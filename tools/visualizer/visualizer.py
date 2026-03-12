import socket
import sys
import argparse
import pygame

# Protocol Types
VISUALIZER_TYPE_PIXELS = 0x00
VISUALIZER_TYPE_METADATA = 0x01

def main():
    parser = argparse.ArgumentParser(description="ChimeraFX UDP Visualizer")
    parser.add_argument("--ip", default="0.0.0.0", help="IP address to bind to (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=7777, help="UDP port (default: 7777)")
    parser.add_argument("--rgbw", action="store_true", help="Assume RGBW (4 bytes per pixel) instead of RGB (3 bytes)")
    parser.add_argument("--scale", type=int, default=15, help="Size of each LED square in pixels (default: 15)")
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

    clock = pygame.Clock()
    
    # State
    pixels = []
    effect_name = "Waiting for metadata..."
    palette_name = ""
    
    bytes_per_pixel = 4 if args.rgbw else 3
    running = True

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False

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
            
            text_surf = font.render(text_str, True, (255, 255, 255))
            screen.blit(text_surf, (10, 10))
            
            pygame.display.flip()

        clock.tick(60)

    pygame.quit()
    sys.exit(0)

if __name__ == "__main__":
    main()
