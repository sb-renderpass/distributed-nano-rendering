import math
import socket
import struct

SERVER_PORT         = 3333
FRAME_BUFFER_WIDTH  = 320
FRAME_BUFFER_HEIGHT = 240
PKT_HEADER_SIZE     = 4
PKT_BUFFER_SIZE     = 1400
NUM_SLICES          = 8
POSE_SIZE           = 32

FRAME_BUFFER_SIZE   = FRAME_BUFFER_WIDTH * FRAME_BUFFER_HEIGHT
SLICE_BUFFER_SIZE   = FRAME_BUFFER_SIZE // NUM_SLICES

NUM_PKTS_PER_SLICE  = int(math.ceil(SLICE_BUFFER_SIZE / PKT_BUFFER_SIZE))
NUM_PKTS_PER_FRAME  = NUM_PKTS_PER_SLICE * NUM_SLICES
LAST_PKT_ID         = NUM_PKTS_PER_FRAME - 1

def write_pkt_header(padding: int, seqnum: int, offset: int, buffer):
    buffer[0] = (padding << 7) | (seqnum & 0x7F)
    buffer[1] = (offset >> 16) & 0xFF
    buffer[2] = (offset >>  8) & 0xFF
    buffer[3] = (offset >>  0) & 0xFF

def main():
    print("[ Test Server ]")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.settimeout(10) # TODO: Use SO_RCVTIMEO
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_DONTROUTE, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,    POSE_SIZE)
    sock.bind(("", SERVER_PORT))

    PKT_PAYLOAD_SIZE = PKT_BUFFER_SIZE - PKT_HEADER_SIZE
    REM_SIZE_PER_SLICE = SLICE_BUFFER_SIZE % PKT_PAYLOAD_SIZE

    frame_num = 0

    while True:
        _, client_addr = sock.recvfrom(POSE_SIZE)

        seqnum = 0
        offset = 0

        for slice_id in range(NUM_SLICES):
            pkt_buffer = bytearray(PKT_BUFFER_SIZE)

            # Render slices as distinct colored stripes
            pkt_buffer[:] = [int((slice_id + 1) / (NUM_SLICES + 1) * 0xFF) for _ in range(PKT_BUFFER_SIZE)]

            for i in range(NUM_PKTS_PER_SLICE):
                is_last_pkt = (i == (NUM_PKTS_PER_SLICE - 1))
                write_pkt_header(is_last_pkt, seqnum, offset, pkt_buffer)
                payload_size = REM_SIZE_PER_SLICE if is_last_pkt else PKT_PAYLOAD_SIZE

                if is_last_pkt:
                    padding = PKT_PAYLOAD_SIZE - REM_SIZE_PER_SLICE
                    pkt_buffer[-2:] = struct.pack("<H", padding)

                # TODO: Write telemetry on last packet in frame

                sock.sendto(pkt_buffer, client_addr)

                seqnum += 1
                offset += payload_size

        print(f"frame {frame_num:4d}")
        frame_num += 1

if __name__ == "__main__":
    main()

