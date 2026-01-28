#!/usr/bin/env python3
"""
Build a PSX disc image (.bin/.cue) from the compiled executable.
Includes CD-DA audio track for music playback.
Requires: mkpsxiso (https://github.com/Lameguy64/mkpsxiso)
"""

import os
import sys
import subprocess
import shutil
import wave
import struct
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
OUTPUT_DIR = PROJECT_ROOT / "web" / "rom"
ASSETS_DIR = PROJECT_ROOT / "assets"

def create_iso_xml(has_audio=False):
    """Create the XML configuration for mkpsxiso."""
    audio_track = ""
    if has_audio:
        audio_track = '''
    <track type="audio" source="audio.pcm"/>'''

    xml_content = f'''<?xml version="1.0" encoding="UTF-8"?>
<iso_project image_name="lander.bin" cue_sheet="lander.cue">
    <track type="data">
        <identifiers
            system="PLAYSTATION"
            application="PLAYSTATION"
            volume="LANDER"
            volume_set="LANDER"
            publisher="BARE_METAL"
            data_preparer="MKPSXISO"
        />
        <directory_tree>
            <file name="SYSTEM.CNF" source="system.cnf"/>
            <file name="LANDER.EXE" source="lander.psexe"/>
        </directory_tree>
    </track>{audio_track}
</iso_project>
'''
    return xml_content

def create_system_cnf():
    """Create SYSTEM.CNF boot configuration."""
    return "BOOT=cdrom:\\LANDER.EXE;1\nTCB=4\nEVENT=10\nSTACK=801FFFF0\n"

def convert_wav_to_cdda(wav_path, output_path):
    """Convert WAV file to raw CD-DA format (16-bit stereo 44100Hz PCM)."""
    print(f"Converting {wav_path.name} to CD audio...")

    try:
        with wave.open(str(wav_path), 'rb') as wav:
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            framerate = wav.getframerate()
            n_frames = wav.getnframes()

            print(f"  Source: {channels}ch, {sample_width*8}bit, {framerate}Hz, {n_frames} frames")

            # Read all audio data
            audio_data = wav.readframes(n_frames)

        # Convert to CD-DA format: 16-bit stereo 44100Hz
        # If mono, duplicate to stereo
        if channels == 1 and sample_width == 2:
            # Mono 16-bit -> Stereo 16-bit
            samples = struct.unpack(f'<{n_frames}h', audio_data)
            stereo_data = b''.join(struct.pack('<hh', s, s) for s in samples)
            audio_data = stereo_data
        elif channels == 1 and sample_width == 1:
            # Mono 8-bit -> Stereo 16-bit
            samples = struct.unpack(f'{n_frames}B', audio_data)
            stereo_data = b''.join(struct.pack('<hh', (s-128)*256, (s-128)*256) for s in samples)
            audio_data = stereo_data
        elif channels == 2 and sample_width == 1:
            # Stereo 8-bit -> Stereo 16-bit
            samples = struct.unpack(f'{n_frames*2}B', audio_data)
            audio_data = b''.join(struct.pack('<h', (s-128)*256) for s in samples)

        # If sample rate is not 44100, we'd need resampling (skip for now)
        if framerate != 44100:
            print(f"  Warning: Sample rate is {framerate}Hz, CD-DA requires 44100Hz")
            print(f"  Audio may play at wrong speed")

        # Write raw PCM data
        with open(output_path, 'wb') as f:
            f.write(audio_data)

        # CD-DA sectors are 2352 bytes, pad to sector boundary
        file_size = output_path.stat().st_size
        sector_size = 2352
        padding_needed = (sector_size - (file_size % sector_size)) % sector_size
        if padding_needed > 0:
            with open(output_path, 'ab') as f:
                f.write(b'\x00' * padding_needed)

        final_size = output_path.stat().st_size
        print(f"  Output: stereo 16-bit, {final_size} bytes ({final_size // sector_size} sectors)")
        return True

    except Exception as e:
        print(f"  Error converting audio: {e}")
        return False

def main():
    # Check for mkpsxiso
    mkpsxiso = shutil.which("mkpsxiso")
    if not mkpsxiso:
        # Check common locations
        possible_paths = [
            PROJECT_ROOT / "tools" / "mkpsxiso" / "mkpsxiso.exe",
            PROJECT_ROOT / "tools" / "mkpsxiso.exe",
            PROJECT_ROOT / "tools" / "mkpsxiso",
            Path("C:/mkpsxiso/bin/mkpsxiso.exe"),
        ]
        for p in possible_paths:
            if p.exists():
                mkpsxiso = str(p)
                break

    if not mkpsxiso:
        print("ERROR: mkpsxiso not found!")
        print("Download from: https://github.com/Lameguy64/mkpsxiso/releases")
        print("Place mkpsxiso.exe in the tools/ folder")
        sys.exit(1)

    # Check for compiled executable
    exe_path = BUILD_DIR / "lander.psexe"
    if not exe_path.exists():
        print(f"ERROR: {exe_path} not found!")
        print("Run 'cmake --build build' first")
        sys.exit(1)

    # Create working directory
    work_dir = BUILD_DIR / "iso_work"
    work_dir.mkdir(exist_ok=True)

    # Copy executable
    shutil.copy(exe_path, work_dir / "lander.psexe")

    # Create SYSTEM.CNF
    (work_dir / "system.cnf").write_text(create_system_cnf())

    # Audio is now embedded in executable via SPU, no CD-DA needed
    has_audio = False

    if not has_audio:
        print("No audio file found, creating data-only disc")

    # Create ISO XML config
    xml_path = work_dir / "iso.xml"
    xml_path.write_text(create_iso_xml(has_audio))

    # Run mkpsxiso
    print("Building disc image...")
    print(f"Using: {mkpsxiso}")

    # Delete old output files if they exist
    import time
    timestamp = int(time.time())
    temp_bin = f"lander_{timestamp}.bin"
    temp_cue = f"lander_{timestamp}.cue"

    # Update XML to use temp filenames
    xml_content = xml_path.read_text()
    xml_content = xml_content.replace('image_name="lander.bin"', f'image_name="{temp_bin}"')
    xml_content = xml_content.replace('cue_sheet="lander.cue"', f'cue_sheet="{temp_cue}"')
    xml_path.write_text(xml_content)

    # Use shell=True on Windows to avoid permission issues
    # Run from work_dir so paths are relative
    cmd = f'"{mkpsxiso}" iso.xml -y'
    result = subprocess.run(
        cmd,
        cwd=work_dir,
        capture_output=True,
        text=True,
        shell=True
    )

    if result.returncode != 0:
        print("ERROR: mkpsxiso failed!")
        print(result.stdout)
        print(result.stderr)
        sys.exit(1)

    # Copy output to web/rom
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    bin_path = work_dir / temp_bin
    cue_path = work_dir / temp_cue

    if bin_path.exists():
        # Copy to final location, overwriting if needed
        dest_bin = OUTPUT_DIR / "lander.bin"
        dest_cue = OUTPUT_DIR / "lander.cue"

        # Check if files are locked before attempting to copy
        for dest in [dest_bin, dest_cue]:
            if dest.exists():
                try:
                    dest.unlink()
                except PermissionError:
                    print(f"ERROR: {dest} is locked by another process!")
                    print("Close the emulator or server and try again.")
                    sys.exit(1)

        try:
            shutil.copy(bin_path, dest_bin)
        except PermissionError:
            print(f"ERROR: Cannot write to {dest_bin} - file is locked!")
            print("Close the emulator or server and try again.")
            sys.exit(1)

        # Fix cue file to reference correct filename
        cue_content = cue_path.read_text()
        cue_content = cue_content.replace(temp_bin, "lander.bin")
        dest_cue.write_text(cue_content)

        # Clean up temp files
        try:
            bin_path.unlink()
            cue_path.unlink()
        except:
            pass

        print(f"Created: {dest_bin}")
        print(f"Created: {dest_cue}")
    else:
        print("ERROR: Output files not created")
        sys.exit(1)

    print("Done!")

if __name__ == "__main__":
    main()
