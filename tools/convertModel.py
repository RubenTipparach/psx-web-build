#!/usr/bin/env python3
"""
Convert OBJ model files to PS1 binary format.

Output format:
  Header (8 bytes):
    uint16_t num_vertices
    uint16_t num_uvs
    uint16_t num_faces
    uint16_t reserved

  Vertices (num_vertices * 8 bytes each, matching GTEVector16):
    int16_t x, y, z, padding

  UVs (num_uvs * 2 bytes each):
    uint8_t u, v

  Faces (num_faces * 18 bytes each):
    int16_t v0, v1, v2, v3  (v3 = -1 for triangles)
    int16_t uv0, uv1, uv2, uv3
    int16_t normal_index
"""

import argparse
import struct
import sys
from pathlib import Path


def parse_obj(filepath):
    """Parse OBJ file and return vertices, uvs, and faces."""
    vertices = []
    uvs = []
    faces = []

    # OBJ indices are 1-based, we need to track the base index for each object
    vertex_offset = 0
    uv_offset = 0

    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            parts = line.split()
            if not parts:
                continue

            cmd = parts[0]

            if cmd == 'v':
                # Vertex position
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                vertices.append((x, y, z))

            elif cmd == 'vt':
                # Texture coordinate
                u, v = float(parts[1]), float(parts[2])
                uvs.append((u, v))

            elif cmd == 'f':
                # Face - can be triangle or quad
                face_verts = []
                face_uvs = []

                for i in range(1, len(parts)):
                    indices = parts[i].split('/')
                    v_idx = int(indices[0]) - 1  # Convert to 0-based

                    if len(indices) >= 2 and indices[1]:
                        uv_idx = int(indices[1]) - 1
                    else:
                        uv_idx = 0

                    face_verts.append(v_idx)
                    face_uvs.append(uv_idx)

                # Handle triangles and quads
                # Reverse winding order (swap v1/v2) for correct backface culling on PS1
                if len(face_verts) == 3:
                    faces.append({
                        'verts': (face_verts[0], face_verts[2], face_verts[1], -1),
                        'uvs': (face_uvs[0], face_uvs[2], face_uvs[1], -1)
                    })
                elif len(face_verts) == 4:
                    # Split quad into two triangles (with reversed winding)
                    faces.append({
                        'verts': (face_verts[0], face_verts[2], face_verts[1], -1),
                        'uvs': (face_uvs[0], face_uvs[2], face_uvs[1], -1)
                    })
                    faces.append({
                        'verts': (face_verts[0], face_verts[3], face_verts[2], -1),
                        'uvs': (face_uvs[0], face_uvs[3], face_uvs[2], -1)
                    })
                elif len(face_verts) > 4:
                    # Triangulate polygon using fan method (with reversed winding)
                    for i in range(1, len(face_verts) - 1):
                        faces.append({
                            'verts': (face_verts[0], face_verts[i+1], face_verts[i], -1),
                            'uvs': (face_uvs[0], face_uvs[i+1], face_uvs[i], -1)
                        })

    return vertices, uvs, faces


def convert_to_binary(vertices, uvs, faces, scale=28.0, tex_size=64):
    """Convert parsed OBJ data to binary format."""
    data = bytearray()

    # Header
    data.extend(struct.pack('<HHHH',
        len(vertices),
        len(uvs),
        len(faces),
        0  # reserved
    ))

    # Vertices (scaled and converted to int16)
    # GTEVector16 is 8 bytes: x, y, z, padding (each int16)
    for x, y, z in vertices:
        # Scale and convert to 16-bit integers
        # Swap Y and Z for PS1 coordinate system, negate Y
        vx = int(x * scale)
        vy = int(-z * scale)  # PS1 Y is up
        vz = int(y * scale)   # PS1 Z is depth

        # Clamp to int16 range
        vx = max(-32768, min(32767, vx))
        vy = max(-32768, min(32767, vy))
        vz = max(-32768, min(32767, vz))

        # Pack as 8 bytes: x, y, z, padding (matching GTEVector16 structure)
        data.extend(struct.pack('<hhhh', vx, vy, vz, 0))

    # UVs (converted to 0-255 range based on texture size)
    for u, v in uvs:
        # OBJ UVs are 0-1, convert to pixel coordinates
        # V is flipped in OBJ (0 = bottom, 1 = top)
        pu = int(u * tex_size) % 256
        pv = int((1.0 - v) * tex_size) % 256

        data.extend(struct.pack('<BB', pu, pv))

    # Pad to 4-byte alignment
    while len(data) % 4 != 0:
        data.append(0)

    # Faces
    for face in faces:
        v0, v1, v2, v3 = face['verts']
        uv0, uv1, uv2, uv3 = face['uvs']

        data.extend(struct.pack('<hhhh', v0, v1, v2, v3))
        data.extend(struct.pack('<hhhh', uv0, uv1, uv2, uv3))
        data.extend(struct.pack('<h', 0))  # normal index (unused)

    # Pad to 4-byte alignment
    while len(data) % 4 != 0:
        data.append(0)

    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description='Convert OBJ to PS1 binary format')
    parser.add_argument('input', help='Input OBJ file')
    parser.add_argument('output', help='Output binary file')
    parser.add_argument('-s', '--scale', type=float, default=28.0,
                        help='Scale factor for vertices (default: 28.0)')
    parser.add_argument('-t', '--texsize', type=int, default=64,
                        help='Texture size in pixels (default: 64)')

    args = parser.parse_args()

    print(f"Converting {args.input} to {args.output}")

    vertices, uvs, faces = parse_obj(args.input)
    print(f"  Vertices: {len(vertices)}")
    print(f"  UVs: {len(uvs)}")
    print(f"  Faces: {len(faces)}")

    binary_data = convert_to_binary(vertices, uvs, faces, args.scale, args.texsize)
    print(f"  Output size: {len(binary_data)} bytes")

    # Ensure output directory exists
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)

    with open(args.output, 'wb') as f:
        f.write(binary_data)

    print("Done!")


if __name__ == '__main__':
    main()
