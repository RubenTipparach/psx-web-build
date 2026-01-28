#!/usr/bin/env python3
"""
Build a PSX disc image (.bin/.cue) from the compiled executable.
Includes CD-DA audio track for music and SPU for sound effects.
Requires: mkpsxiso
"""

import sys
import subprocess
import shutil
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
OUTPUT_DIR = PROJECT_ROOT / "web" / "rom"
TOOLS_DIR = PROJECT_ROOT / "tools"
ASSETS_DIR = PROJECT_ROOT / "assets"

def create_iso_xml(has_audio=False):
    """Create XML for disc with optional CD-DA audio track."""
    xml = '''<?xml version="1.0" encoding="UTF-8"?>

<iso_project image_name="lander.bin" cue_sheet="lander.cue">
\t<track type="data">
\t\t<directory_tree>
\t\t\t<file name="SYSTEM.CNF" source="system.cnf"/>
\t\t\t<file name="LANDER.EXE" source="lander.psexe"/>
\t\t</directory_tree>
\t</track>
'''
    if has_audio:
        xml += '''\t<track type="audio" source="music.wav"/>
'''
    xml += '''</iso_project>
'''
    return xml

def create_system_cnf():
    """Create SYSTEM.CNF boot configuration."""
    return "BOOT=cdrom:\\LANDER.EXE;1\nTCB=4\nEVENT=10\nSTACK=801FFFF0\n"

def main():
    # Find mkpsxiso
    mkpsxiso = None
    possible_mkpsxiso = [
        PROJECT_ROOT / "web" / "rom" / "PSX Snake Alpha Source Code and Assets" / "Source Code and Assets" / "Source Code" / "mkpsxiso" / "mkpsxiso.exe",
        TOOLS_DIR / "mkpsxiso" / "mkpsxiso.exe",
        TOOLS_DIR / "mkpsxiso.exe",
        Path("C:/mkpsxiso/bin/mkpsxiso.exe"),
    ]
    for p in possible_mkpsxiso:
        if p.exists():
            mkpsxiso = str(p)
            break
    if not mkpsxiso:
        mkpsxiso = shutil.which("mkpsxiso")

    if not mkpsxiso:
        print("ERROR: mkpsxiso not found!")
        sys.exit(1)
    print(f"Found mkpsxiso: {mkpsxiso}")

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
    exe_size = exe_path.stat().st_size / 1024
    print(f"Executable size: {exe_size:.1f} KB")

    # Check for CD-DA music file (SefChol - Take it Slow for testing)
    music_file = ASSETS_DIR / "sefchol_take_it_slow.wav"
    has_audio = music_file.exists()
    if has_audio:
        shutil.copy(music_file, work_dir / "music.wav")
        music_size = music_file.stat().st_size / (1024 * 1024)
        print(f"CD-DA music: {music_size:.1f} MB (SefChol - Take it Slow)")
    else:
        print("No CD-DA music (sefchol_take_it_slow.wav not found)")

    # Create SYSTEM.CNF
    (work_dir / "system.cnf").write_text(create_system_cnf())

    # Create ISO XML config
    xml_path = work_dir / "iso.xml"
    xml_path.write_text(create_iso_xml(has_audio))

    # Run mkpsxiso
    print("Building disc image...")

    import time
    timestamp = int(time.time())
    temp_bin = f"lander_{timestamp}.bin"
    temp_cue = f"lander_{timestamp}.cue"

    # Update XML to use temp filenames
    xml_content = xml_path.read_text()
    xml_content = xml_content.replace('image_name="lander.bin"', f'image_name="{temp_bin}"')
    xml_content = xml_content.replace('cue_sheet="lander.cue"', f'cue_sheet="{temp_cue}"')
    xml_path.write_text(xml_content)

    cmd = f'"{mkpsxiso}" iso.xml -y'
    result = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True, shell=True)

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
        dest_bin = OUTPUT_DIR / "lander.bin"
        dest_cue = OUTPUT_DIR / "lander.cue"

        for dest in [dest_bin, dest_cue]:
            if dest.exists():
                try:
                    dest.unlink()
                except PermissionError:
                    print(f"ERROR: {dest} is locked!")
                    sys.exit(1)

        shutil.copy(bin_path, dest_bin)

        cue_content = cue_path.read_text()
        cue_content = cue_content.replace(temp_bin, "lander.bin")
        dest_cue.write_text(cue_content)

        # Clean up
        try:
            bin_path.unlink()
            cue_path.unlink()
        except:
            pass

        print(f"Created: {dest_bin}")
        print(f"Created: {dest_cue}")

        # Create zip for EmulatorJS
        import zipfile
        dest_zip = OUTPUT_DIR / "lander.zip"
        try:
            if dest_zip.exists():
                dest_zip.unlink()
            with zipfile.ZipFile(dest_zip, 'w', zipfile.ZIP_DEFLATED) as zf:
                zf.write(dest_bin, "lander.bin")
                zf.write(dest_cue, "lander.cue")
            print(f"Created: {dest_zip}")
        except Exception as e:
            print(f"Warning: Could not create zip: {e}")
    else:
        print("ERROR: Output files not created")
        sys.exit(1)

    print("Done!")

if __name__ == "__main__":
    main()
