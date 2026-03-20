import socket
import sys
import argparse
import os
import subprocess
import pygame
import numpy as np
import cv2
from collections import deque

# Protocol Types
VISUALIZER_TYPE_PIXELS = 0x00
VISUALIZER_TYPE_METADATA = 0x01

# Pre-buffer: frames to keep before recording starts (~500ms at 60fps)
PRE_BUFFER_SIZE = 30
# Post-buffer: frames to keep recording after is_lit goes false (~1s fade tail)
POST_BUFFER_SIZE = 60
# CRF quality for VP9 WebM encoding (0=lossless, 63=worst).
# VP9 is ~2x more efficient than VP8 for the same quality.
# 50 = good quality (~53KB per 12s) matching original visualizer output
# 40 = higher quality (~83KB per 12s)
ENCODE_CRF = 50


def get_filename_base(effect_name, palette_name=None):
    # Palette name intentionally excluded — effect name only.
    eff = "".join([c for c in effect_name if c.isalpha() or c.isdigit() or c in " -_"]).strip()
    return eff.replace(" ", "_")


def encode_webm(raw_path, out_path, crf=10):
    """Re-encode raw AVI to compact WebM using VP9 Constant Quality mode.
    VP9 + CRF + b:v=0 matches the original visualizer output quality and size.
    - crf=50: ~53KB per 12s (matches original)
    - crf=40: ~83KB per 12s (higher quality)
    """
    cmd = [
        "ffmpeg", "-y",
        "-i", raw_path,
        "-c:v", "libvpx-vp9",  # VP9: ~2x better compression than VP8
        "-crf", str(crf),
        "-b:v", "0",           # MUST be 0 for pure CRF mode
        "-deadline", "good",
        "-cpu-used", "2",
        out_path
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=120)
        if result.returncode == 0:
            raw_size = os.path.getsize(raw_path)
            out_size = os.path.getsize(out_path)
            print(f"Encoded: {out_path} ({out_size//1024} KB, from {raw_size//1024} KB raw)")
        else:
            print(f"ffmpeg error: {result.stderr.decode()[-300:]}")
    except Exception as e:
        print(f"Encode failed: {e}")
    finally:
        if os.path.exists(raw_path):
            os.remove(raw_path)


def capture_snapshot(screen, pixels, scale, effect_name, palette_name):
    if not pixels:
        print("No pixels to capture.")
        return
    box_width = 1200
    box_height = 60
    snap_surface = pygame.Surface((box_width, box_height), pygame.SRCALPHA)
    snap_surface.fill((39, 41, 41))
    start_x = 20
    start_y = 20
    for i, color in enumerate(pixels):
        if i >= 58: break
        x = start_x + (i * scale)
        inner_pad = 2
        pygame.draw.rect(snap_surface, (color[0], color[1], color[2], 255),
                         (x + inner_pad, start_y + inner_pad, scale - inner_pad*2, scale - inner_pad*2))
    os.makedirs("examples", exist_ok=True)
    base_name = get_filename_base(effect_name)
    filepath = os.path.join("examples", f"Snapshot_{base_name}.png")
    pygame.image.save(snap_surface, filepath)
    print(f"Snapshot saved: {filepath}")


def apply_brightness_gamma(pixels, brightness, gamma):
    """Apply brightness multiplier and gamma correction to pixel list."""
    inv_gamma = 1.0 / gamma if gamma > 0 else 1.0
    result = []
    for r, g, b in pixels:
        r2 = min(255, int(((r / 255.0) ** inv_gamma) * 255 * brightness))
        g2 = min(255, int(((g / 255.0) ** inv_gamma) * 255 * brightness))
        b2 = min(255, int(((b / 255.0) ** inv_gamma) * 255 * brightness))
        result.append((r2, g2, b2))
    return result


def main():
    parser = argparse.ArgumentParser(description="ChimeraFX UDP Visualizer")
    parser.add_argument("--ip", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--rgbw", action="store_true", default=True)
    parser.add_argument("--scale", type=int, default=20)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.ip, args.port))
    sock.setblocking(False)
    print(f"Listening on {args.ip}:{args.port}...")
    print("Keys: S=snapshot  R=manual record  B/shift+B=brightness  G/shift+G=gamma  ESC=quit")

    pygame.init()
    pygame.display.set_caption("ChimeraFX Visualizer")
    screen = None
    led_count = 0
    font = pygame.font.SysFont("Arial", 18)
    clock = pygame.time.Clock()

    pixels = []
    effect_name = "Waiting for metadata..."
    palette_name = ""
    bytes_per_pixel = 4 if args.rgbw else 3
    running = True

    # Brightness and gamma controls
    brightness = 8.0      # multiplier: 1.0 = raw, 2.0 = double
    gamma = 1.0           # <1.0 lifts shadows, >1.0 darkens midtones
    BRIGHTNESS_STEP = 0.1
    GAMMA_STEP = 0.05

    # Recording state
    is_recording = False
    raw_writer = None      # writes uncompressed AVI during capture
    raw_filepath = ""
    recording_filepath = ""
    current_recorded_effect = None
    post_buffer_remaining = 0

    # Pre-buffer: rolling window of rendered BGR frames
    pre_buffer = deque(maxlen=PRE_BUFFER_SIZE)

    def render_frame_to_bgr(screen):
        try:
            sub = screen.subsurface((0, 40, 1200, 60))
            view = pygame.surfarray.array3d(sub)
            frame = np.transpose(view, (1, 0, 2))
            return cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
        except Exception as e:
            print(f"Frame capture error: {e}")
            return None

    def start_recording(eff_name, pal_name):
        nonlocal is_recording, raw_writer, raw_filepath, recording_filepath
        if raw_writer:
            raw_writer.release()
        os.makedirs("examples", exist_ok=True)
        base_name = get_filename_base(eff_name)
        raw_filepath = os.path.join("examples", f"_{base_name}_raw.avi")
        recording_filepath = os.path.join("examples", f"{base_name}.webm")
        # Write uncompressed AVI — ffmpeg re-encodes at stop
        fourcc = cv2.VideoWriter_fourcc(*'MJPG')
        raw_writer = cv2.VideoWriter(raw_filepath, fourcc, 60.0, (1200, 60))
        flushed = len(pre_buffer)
        for frame in pre_buffer:
            raw_writer.write(frame)
        pre_buffer.clear()
        is_recording = True
        print(f"Recording started: {recording_filepath} (flushed {flushed} pre-buffer frames)")

    def stop_recording():
        nonlocal is_recording, raw_writer
        if raw_writer:
            raw_writer.release()
            raw_writer = None
        is_recording = False
        print(f"Encoding {recording_filepath}...")
        encode_webm(raw_filepath, recording_filepath, ENCODE_CRF)

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                mods = pygame.key.get_mods()
                shift = mods & pygame.KMOD_SHIFT
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_s:
                    capture_snapshot(screen, pixels, args.scale, effect_name, palette_name)
                elif event.key == pygame.K_r:
                    if not is_recording and pixels:
                        start_recording(effect_name, palette_name)
                    elif is_recording:
                        stop_recording()
                elif event.key == pygame.K_b:
                    brightness = max(0.1, brightness + (-BRIGHTNESS_STEP if shift else BRIGHTNESS_STEP))
                    print(f"Brightness: {brightness:.2f}")
                elif event.key == pygame.K_g:
                    gamma = max(0.1, gamma + (-GAMMA_STEP if shift else GAMMA_STEP))
                    print(f"Gamma: {gamma:.2f}")

        # Read UDP packets
        while True:
            try:
                data, addr = sock.recvfrom(65535)
                if not data:
                    break
                packet_type = data[0]
                if packet_type == VISUALIZER_TYPE_PIXELS:
                    raw_pixels = data[1:]
                    new_led_count = len(raw_pixels) // bytes_per_pixel
                    if new_led_count != led_count and new_led_count > 0:
                        led_count = new_led_count
                        screen = pygame.display.set_mode((1200, 100))
                        print(f"Detected {led_count} LEDs")
                    pixels = []
                    for i in range(led_count):
                        idx = i * bytes_per_pixel
                        if idx + 2 < len(raw_pixels):
                            # GRB wire order (WS2812/SK6812 native)
                            g = raw_pixels[idx]
                            r = raw_pixels[idx + 1]
                            b = raw_pixels[idx + 2]
                            w = raw_pixels[idx + 3] if bytes_per_pixel == 4 and idx + 3 < len(raw_pixels) else 0
                            pixels.append((min(255, r + w), min(255, g + w), min(255, b + w)))
                elif packet_type == VISUALIZER_TYPE_METADATA:
                    if len(data) >= 4 and data[1:4] == b"CFX":
                        payload = data[4:].decode('utf-8', errors='ignore')
                        if ':' in payload:
                            effect_name, palette_name = payload.split(':', 1)
                        else:
                            effect_name = payload
                            palette_name = ""
            except BlockingIOError:
                break
            except Exception as e:
                print(f"UDP Error: {e}")
                break

        # Auto recording logic
        is_lit = any(r > 0 or g > 0 or b > 0 for r, g, b in pixels)
        is_valid_effect = effect_name and effect_name not in ["Solid", "Waiting for metadata..."]

        if is_valid_effect and is_lit:
            if not is_recording or current_recorded_effect != effect_name:
                start_recording(effect_name, palette_name)
                current_recorded_effect = effect_name
            post_buffer_remaining = POST_BUFFER_SIZE
        elif is_recording:
            if post_buffer_remaining > 0:
                post_buffer_remaining -= 1
            else:
                stop_recording()
                current_recorded_effect = None
                post_buffer_remaining = 0

        # Render
        if screen is not None:
            screen.fill((20, 20, 20))
            pygame.draw.rect(screen, (39, 41, 41), (0, 40, 1200, 60))

            # Apply brightness + gamma for display
            display_pixels = apply_brightness_gamma(pixels, brightness, gamma)

            start_x = 20
            start_y = 60  # 40 (track_y) + 20 (inner pad)
            for i, color in enumerate(display_pixels):
                if i >= 58: break
                x = start_x + (i * args.scale)
                inner_pad = 2
                pygame.draw.rect(screen, color,
                    (x + inner_pad, start_y + inner_pad, args.scale - inner_pad*2, args.scale - inner_pad*2))

            # HUD
            text_str = f"Effect: {effect_name}"
            if palette_name:
                text_str += f" | {palette_name}"
            text_str += f"  B:{brightness:.1f} G:{gamma:.2f}"
            if is_recording:
                text_str += "  [REC]"
                text_color = (255, 50, 50)
            else:
                text_color = (200, 200, 200)
            screen.blit(font.render(text_str, True, text_color), (10, 12))
            pygame.display.flip()

            # Capture displayed frame (brightness/gamma applied) — what you see is what gets saved.
            frame_bgr = render_frame_to_bgr(screen)
            if frame_bgr is not None:
                if is_recording and raw_writer:
                    raw_writer.write(frame_bgr)
                elif not is_recording:
                    pre_buffer.append(frame_bgr)

        clock.tick(60)

    if raw_writer:
        raw_writer.release()
        encode_webm(raw_filepath, recording_filepath, ENCODE_CRF)
    pygame.quit()
    sys.exit(0)


if __name__ == "__main__":
    main()