import math
import socket

SERVER_PORT         = 3333
FRAME_BUFFER_WIDTH  = 320
FRAME_BUFFER_HEIGHT = 240
PKT_BUFFER_SIZE     = 1400
NUM_SLICES          = 8
POSE_SIZE           = 32

FRAME_BUFFER_SIZE   = FRAME_BUFFER_WIDTH * FRAME_BUFFER_HEIGHT
SLICE_BUFFER_SIZE   = FRAME_BUFFER_SIZE // NUM_SLICES

HEADER_SIZE         = 4
NUM_PKTS_PER_SLICE  = int(math.ceil(SLICE_BUFFER_SIZE / PKT_BUFFER_SIZE))
NUM_PKTS_PER_FRAME  = NUM_PKTS_PER_SLICE * NUM_SLICES
LAST_PKT_ID         = NUM_PKTS_PER_FRAME - 1

def write_pkt_header(padding: int, seqnum: int, offset: int, buffer):
    buffer[0] = (padding << 7) | (seqnum & 0x7F)
    buffer[1] = offset >> 16
    buffer[2] = offset >>  8
    buffer[3] = offset

def main():
    print("[ Test Server ]")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.settimeout(10) # TODO: Use SO_RCVTIMEO
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_DONTROUTE, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,    POSE_SIZE)
    sock.bind(("", SERVER_PORT))

    pkt_buffer = bytearray(PKT_BUFFER_SIZE)
    frame_num = 0

    while True:
        _, client_addr = sock.recvfrom(POSE_SIZE)

        for i in range(NUM_PKTS_PER_FRAME):
            write_pkt_header(0, i, 0, pkt_buffer)
            sock.sendto(pkt_buffer, client_addr)

        print(f"frame {frame_num:4d}")
        frame_num += 1

if __name__ == "__main__":
    main()

