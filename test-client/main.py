import collections
import math
import socket
import struct
import time

import numpy as np
import pygame

FRAME_RATE  = 30
FRAME_SCALE = 2
FRAME_TIME  = 1.0 / FRAME_RATE

SERVER_IP   = "192.168.12.180"
SERVER_PORT = 3333

FRAME_BUFFER_WIDTH  = 320
FRAME_BUFFER_HEIGHT = 240
PKT_BUFFER_SIZE     = 1400
NUM_SLICES          = 8

FRAME_BUFFER_SIZE   = FRAME_BUFFER_WIDTH * FRAME_BUFFER_HEIGHT
SLICE_BUFFER_SIZE   = FRAME_BUFFER_SIZE // NUM_SLICES

HEADER_SIZE         = 4
NUM_PKTS_PER_SLICE  = int(math.ceil(SLICE_BUFFER_SIZE / PKT_BUFFER_SIZE))
NUM_PKTS_PER_FRAME  = NUM_PKTS_PER_SLICE * NUM_SLICES
LAST_PKT_ID         = NUM_PKTS_PER_FRAME - 1

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(FRAME_TIME)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_DONTROUTE, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 + 6 * 4)
#sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 9000)

avg_win = collections.deque([0 for _ in range(50)])

class Color():
    RED       = "\033[31m"
    YELLOW    = "\033[33m"
    MAGENTA   = "\033[35m"
    CYAN      = "\033[36m"
    WHITE     = "\033[37m"
    RESET     = "\033[0m"

frame_buffer = bytearray(FRAME_BUFFER_SIZE)

pygame.init()
screen_surface = pygame.display.set_mode((FRAME_BUFFER_WIDTH * FRAME_SCALE, FRAME_BUFFER_HEIGHT * FRAME_SCALE))
render_surface = pygame.Surface((FRAME_BUFFER_WIDTH, FRAME_BUFFER_HEIGHT))
pygame.display.set_caption("ESP32 Remote Render")
clock = pygame.time.Clock()
running = True

SPRINT_SPEED = 0.1
STRAFE_SPEED = 0.1
ROTATE_SPEED = 0.05
FOV          = 60
FOV_SCALE    = math.tan(math.radians(FOV / 2))

pos_x   = 22.0
pos_y   = 11.5
dir_x   = -1
dir_y   =  0
plane_x = -dir_y * FOV_SCALE
plane_y = +dir_x

prev_pos_x = pos_x
prev_pos_y = pos_y
pos_dlt_x = 0
pos_dlt_y = 0

prev_dir_x = dir_x
prev_dir_y = dir_y
dir_dlt_x = 0
dir_dlt_y = 0

while running:
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
        elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
            running = False

    keys = pygame.key.get_pressed()
    if keys[pygame.K_w] or keys[pygame.K_UP]:
        pos_x += dir_x * SPRINT_SPEED
        pos_y += dir_y * SPRINT_SPEED
    if keys[pygame.K_s] or keys[pygame.K_DOWN]:
        pos_x -= dir_x * SPRINT_SPEED
        pos_y -= dir_y * SPRINT_SPEED
    if keys[pygame.K_a] or keys[pygame.K_LEFT]:
        pos_x -= plane_x * STRAFE_SPEED
        pos_y -= plane_y * STRAFE_SPEED
    if keys[pygame.K_d] or keys[pygame.K_RIGHT]:
        pos_x += plane_x * STRAFE_SPEED
        pos_y += plane_y * STRAFE_SPEED
    if keys[pygame.K_q]:
        dir_x_ = dir_x
        dir_x = dir_x  * math.cos(-ROTATE_SPEED) - dir_y * math.sin(-ROTATE_SPEED)
        dir_y = dir_x_ * math.sin(-ROTATE_SPEED) + dir_y * math.cos(-ROTATE_SPEED)
        plane_x = -dir_y * FOV_SCALE
        plane_y = +dir_x
    if keys[pygame.K_e]:
        dir_x_ = dir_x
        dir_x = dir_x  * math.cos(+ROTATE_SPEED) - dir_y * math.sin(+ROTATE_SPEED)
        dir_y = dir_x_ * math.sin(+ROTATE_SPEED) + dir_y * math.cos(+ROTATE_SPEED)
        plane_x = -dir_y * FOV_SCALE
        plane_y = +dir_x

    #--- Pose Prediction ---#
    """
    pred_pos_x = prev_pos_x + pos_dlt_x
    pred_pos_y = prev_pos_y + pos_dlt_y
    pos_diff = (abs(pred_pos_x - pos_x), abs(pred_pos_y - pos_y))
    print(f"{pos_diff[0] if pos_diff[0] > 1e-9 else 0},{pos_diff[1] if pos_diff[1] > 1e-9 else 0}")

    pred_dir_x = prev_dir_x * dir_dlt_x - prev_dir_y * dir_dlt_y
    pred_dir_y = prev_dir_x * dir_dlt_y + prev_dir_y * dir_dlt_x
    dir_diff = (abs(pred_dir_x - dir_x), abs(pred_dir_y - dir_y))
    #print(f"{dir_diff[0] if dir_diff[0] > 1e-9 else 0},{dir_diff[1] if dir_diff[1] > 1e-9 else 0}")

    pos_dlt_x  = pos_x - prev_pos_x
    pos_dlt_y  = pos_y - prev_pos_y
    prev_pos_x = pos_x
    prev_pos_y = pos_y

    dir_dlt_x  = dir_x * prev_dir_x + dir_y * prev_dir_y
    dir_dlt_y  = dir_y * prev_dir_x - dir_x * prev_dir_y
    prev_dir_x = dir_x
    prev_dir_y = dir_y
    """

    ts0 = time.time_ns()
    sock.sendto(struct.pack("<Qffffff", ts0, pos_x, pos_y, dir_x, dir_y, plane_x, plane_y), (SERVER_IP, SERVER_PORT))

    prev_pkt_id = -1
    render_elapsed = 0
    stream_elapsed = 0
    ts0_ = ts0

    while prev_pkt_id < LAST_PKT_ID:
        try:
            chunk_buffer = sock.recv(PKT_BUFFER_SIZE)
        except TimeoutError:
            break

        header  = struct.unpack("!I", chunk_buffer[:HEADER_SIZE])[0]
        padding = (header >> 31) & 0x01 
        pkt_id  = (header >> 24) & 0x7F
        offset  = (header >>  0) & 0xFFFFFF

        padding_size = struct.unpack_from("<H", chunk_buffer, PKT_BUFFER_SIZE - 2)[0]
        payload_size = PKT_BUFFER_SIZE - HEADER_SIZE - (padding_size if padding else 0)

        frame_buffer[offset:offset + payload_size] = chunk_buffer[HEADER_SIZE:HEADER_SIZE + payload_size]

        if pkt_id == LAST_PKT_ID:
            render_elapsed, stream_elapsed, ts0_ = struct.unpack_from("<IIQ", chunk_buffer, len(chunk_buffer) - 2 - 4 - 4 - 8)
            render_elapsed *= 1e-3
            stream_elapsed *= 1e-3

        if prev_pkt_id + 1 != pkt_id:
            print(f"{Color.YELLOW}JUMP {prev_pkt_id} => {pkt_id}{Color.RESET}")
        prev_pkt_id = pkt_id

    if prev_pkt_id < LAST_PKT_ID:
        print(f"{Color.RED}LOST {prev_pkt_id + 1} => {LAST_PKT_ID}{Color.RESET}")
        continue

    in_texture = np.frombuffer(frame_buffer, dtype=np.uint8).reshape(FRAME_BUFFER_WIDTH, FRAME_BUFFER_HEIGHT)#.transpose()
    r = ((in_texture & 0b11000000) >> 6) << 6
    g = ((in_texture & 0b00111000) >> 3) << 5
    b = ((in_texture & 0b00000111) >> 0) << 5
    #r = ((in_texture & 0b11100000) >> 5) << 5
    #g = ((in_texture & 0b00011100) >> 2) << 5
    #b = ((in_texture & 0b00000011) >> 0) << 5
    out_texture = np.dstack((r, g, b))
    #out_texture = np.dstack((in_texture, in_texture, in_texture))
    if FRAME_SCALE != 1:
        pygame.surfarray.blit_array(render_surface, out_texture)
        pygame.transform.scale(render_surface, (screen_surface.get_width(), screen_surface.get_height()), screen_surface)
    else:
        pygame.surfarray.blit_array(screen_surface, out_texture)
    pygame.display.update()

    ts1 = time.time_ns()
    rtt = (ts1 - ts0_) * (10**-6)

    #--- Statistics ---#

    avg_win.popleft()
    avg_win.append(rtt)
    avg_rtt = sum(avg_win) / len(avg_win)

    frame_elapsed   = (ts1 - ts0) * (10**-6)
    frame_elapsed_s = frame_elapsed * 1e-3
    elapsed_color = Color.RED if frame_elapsed_s > FRAME_TIME else Color.RESET

    print(f"{avg_rtt:5.1f} {rtt:5.1f} {elapsed_color}{frame_elapsed:5.1f}{Color.RESET} {render_elapsed:5.1f} {stream_elapsed:5.1f}")

    clock.tick(FRAME_RATE)
    pygame.display.set_caption(f"ESP32 Remote Render | {clock.get_fps():4.1f} FPS")
    #time.sleep(sleep_time)

pygame.quit()
