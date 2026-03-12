import socket
import sys
import argparse
import os
import datetime
import pygame
import numpy as np
import cv2

# Protocol Types
VISUALIZER_TYPE_PIXELS = 0x00
VISUALIZER_TYPE_METADATA = 0x01

def get_filename_base(effect_name, palette_name):
    # Clean names for filesystem
    eff = "".join([c for c in effect_name if c.isalpha() or c.isdigit() or c in " -_"]).strip()
    pal = "".join([c for c in palette_name if c.isalpha() or c.isdigit() or c in " -_"]).strip()
    base = eff.replace(" ", "_")
    if pal and pal.lower() not in ["default", "none", "solid"]:
        base += f"_{pal.replace(' ', '_')}"
    return base

def capture_snapshot(screen, pixels, scale, effect_name, palette_name):
    if not pixels:
        print("No pixels to capture.")
        return

    # Offscreen flat render (1D row)
    box_width = len(pixels) * scale
    box_height = scale * 3
    
    # Create transparent surface for just the LEDs
    snap_surface = pygame.Surface((box_width, box_height), pygame.SRCALPHA)
    snap_surface.fill((0, 0, 0, 0)) # Fully transparent background
    
    for i, color in enumerate(pixels):
        x = i * scale
        y = scale
        
        # Transparent padding/glow
        inner_pad = 2
        pygame.draw.rect(snap_surface, (color[0], color[1], color[2], 255), 
                         (x + inner_pad, y + inner_pad, scale - inner_pad*2, scale - inner_pad*2))
                         
    os.makedirs("examples", exist_ok=True)
    base_name = get_filename_base(effect_name, palette_name)
    filepath = os.path.join("examples", f"Snapshot_{base_name}.png")
    
    pygame.image.save(snap_surface, filepath)
    print(f"Snapshot saved: {filepath}")

def main():
    parser = argparse.ArgumentParser(description="ChimeraFX UDP Visualizer")
    parser.add_argument("--ip", default="0.0.0.0", help="IP address to bind to (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=7777, help="UDP port (default: 7777)")
    parser.add_argument("--rgbw", action="store_true", help="Assume RGBW (4 bytes per pixel) instead of RGB (3 bytes)")
    parser.add_argument("--scale", type=int, default=20, help="Size of each LED square in pixels (default: 20)")
    args = parser.parse_args()

    # Networking Setup
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.ip, args.port))
    sock.setblocking(False)
    print(f"Listening for ChimeraFX UDP packets on {args.ip}:{args.port}...")

    # Pygame Setup
    pygame.init()
    pygame.display.set_caption("ChimeraFX Visualizer")
    
    # We will dynamically size the window based on the first packet
    screen = None
    led_count = 0
    font = pygame.font.SysFont("Arial", 20)

    clock = pygame.time.Clock()
    
    # State
    pixels = []
    effect_name = "Waiting for metadata..."
    palette_name = ""
    
    bytes_per_pixel = 4 if args.rgbw else 3
    running = True

    # Recording state
    is_recording = False
    video_writer = None
    recording_filepath = ""
    current_recorded_effect = None

    def start_recording(eff_name, pal_name, pxols):
        nonlocal is_recording, video_writer, recording_filepath
        if video_writer:
            video_writer.release()
        os.makedirs("examples", exist_ok=True)
        base_name = get_filename_base(eff_name, pal_name)
        timestamp = datetime.datetime.now().strftime("%H%M%S")
        recording_filepath = os.path.join("examples", f"{base_name}_{timestamp}.webm")
        
        # Offscreen flat render (1D row) for identical docs videos
        b_width = len(pxols) * args.scale
        b_height = args.scale * 3
        
        fourcc = cv2.VideoWriter_fourcc(*'VP80')
        video_writer = cv2.VideoWriter(recording_filepath, fourcc, 60.0, (b_width, b_height))
        is_recording = True
        print(f"Auto-started recording: {recording_filepath}")

    def stop_recording():
        nonlocal is_recording, video_writer
        if video_writer:
            video_writer.release()
            video_writer = None
        is_recording = False
        print(f"Auto-stopped recording: {recording_filepath}")

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_s:
                    capture_snapshot(screen, pixels, args.scale, effect_name, palette_name)
                # Manual recording (R key) overrides auto
                elif event.key == pygame.K_r:
                    if not is_recording and pixels:
                        start_recording(effect_name, palette_name, pixels)
                    elif is_recording:
                        stop_recording()

        # Read all pending UDP packets
        while True:
            try:
                data, addr = sock.recvfrom(65535)
                if not data:
                    break
                
                packet_type = data[0]
                
                if packet_type == VISUALIZER_TYPE_PIXELS:
                    raw_pixels = data[1:]
                    new_led_count = len(raw_pixels) // bytes_per_pixel
                    
                    # Initialize or resize window if LED count changes
                    if new_led_count != led_count and new_led_count > 0:
                        led_count = new_led_count
                        width = led_count * args.scale
                        height = args.scale * 4 + 60 # Extra room for text
                        # Prevent window from being wider than absolute max (wrap around if needed)
                        max_width = 1600
                        if width > max_width:
                            columns = max_width // args.scale
                            rows = (led_count // columns) + 1
                            width = columns * args.scale
                            height = (rows * args.scale) + 60
                        
                        screen = pygame.display.set_mode((width, height))
                        print(f"Detected {led_count} LEDs. Resizing window to {width}x{height}")

                    # Parse colors
                    pixels = []
                    for i in range(led_count):
                        idx = i * bytes_per_pixel
                        if idx + 2 < len(raw_pixels):
                            # Default GRB/RGB order varies, but we'll assume the raw buffer order
                            # Typical WS2812 is GRB, SK6812 is GRBW in memory. 
                            # C++ sends raw `buf_` which is post-mapping. So `buf_` is literally native wire format.
                            # Just map the first 3 bytes as RGB or GRB. Try GRB standard for WS/SK:
                            # Actually, get_view_internal suggests buf_ is stored in requested memory order.
                            # Let's assume Native memory order is RGB for display purposes, but often it's GRB.
                            # We'll map memory byte 0 to R, 1 to G, 2 to B for simplicity, 
                            # though if it's GRB we should swap. We will provide a simple heuristic:
                            r = raw_pixels[idx]
                            g = raw_pixels[idx + 1]
                            b = raw_pixels[idx + 2]
                            w = raw_pixels[idx + 3] if bytes_per_pixel == 4 and idx + 3 < len(raw_pixels) else 0

                            # Add white channel equally to RGB to simulate RGBW visually
                            r_vis = min(255, r + w)
                            g_vis = min(255, g + w)
                            b_vis = min(255, b + w)
                            pixels.append((r_vis, g_vis, b_vis))
                            
                elif packet_type == VISUALIZER_TYPE_METADATA:
                    # Parse Metadata: [0x01] [C] [F] [X] [Name] [:] [Palette]
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

        # --- AUTO RECORDING LOGIC ---
        is_lit = any(r > 0 or g > 0 or b > 0 for r, g, b in pixels)
        is_valid_effect = effect_name and effect_name not in ["Solid", "Waiting for metadata..."]
        
        if is_valid_effect and is_lit:
            if not is_recording or current_recorded_effect != effect_name:
                start_recording(effect_name, palette_name, pixels)
                current_recorded_effect = effect_name
        elif is_recording and (not is_lit or not is_valid_effect):
            # Turned off or effect cleared
            stop_recording()
            current_recorded_effect = None
            
        # Render
        if screen is not None:
            screen.fill((20, 20, 20)) # Dark background
            
            # Draw LEDs
            columns = screen.get_width() // args.scale
            
            for i, color in enumerate(pixels):
                col = i % columns
                row = i // columns
                
                x = col * args.scale
                y = 50 + (row * args.scale)
                
                # Draw LED bounding box
                pygame.draw.rect(screen, (40, 40, 40), (x, y, args.scale, args.scale), 1)
                # Draw LED color
                inner_pad = 2
                pygame.draw.rect(screen, color, (x + inner_pad, y + inner_pad, args.scale - inner_pad*2, args.scale - inner_pad*2))

            # Draw Metadata Text
            text_str = f"Effect: {effect_name}"
            if palette_name:
                text_str += f" | Palette: {palette_name}"
            text_str += f" | LEDs: {led_count}"
            
            if is_recording:
                text_str += " | [REC]"
                text_color = (255, 50, 50)
            else:
                text_color = (255, 255, 255)
            
            text_surf = font.render(text_str, True, text_color)
            screen.blit(text_surf, (10, 10))
            
            pygame.display.flip()
            
            # Handle Video Frame Capture
            if is_recording and video_writer and pixels:
                box_width = len(pixels) * args.scale
                box_height = args.scale * 3
                
                vid_surface = pygame.Surface((box_width, box_height))
                vid_surface.fill((39, 41, 41)) # Dark background matching the original webm
                
                for i, color in enumerate(pixels):
                    x = i * args.scale
                    y = args.scale
                    inner_pad = 2
                    pygame.draw.rect(vid_surface, (color[0], color[1], color[2]), 
                                     (x + inner_pad, y + inner_pad, args.scale - inner_pad*2, args.scale - inner_pad*2))
                
                try:
                    view = pygame.surfarray.pixels3d(vid_surface)
                    # Convert to numpy array and swap axes from (x,y,c) to (y,x,c) expected by OpenCV
                    frame = np.transpose(view, (1, 0, 2))
                    # Convert RGB to BGR
                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                    video_writer.write(frame_bgr)
                except Exception as e:
                    print(f"Frame capture error: {e}")
                    is_recording = False
                    if video_writer:
                        video_writer.release()

        clock.tick(60)

    if video_writer:
        video_writer.release()

    pygame.quit()
    sys.exit(0)

if __name__ == "__main__":
    main()
