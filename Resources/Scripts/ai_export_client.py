#!/usr/bin/env python3
"""
AI Export TCP Client

Connects to MCPToolkit TCP server in Unreal Editor to export assets.
Automatically discovers server port from discovery file.

Usage:
    # Check if editor is running
    python ai_export_client.py ping

    # Export a widget blueprint
    python ai_export_client.py export_widget /Game/UI/W_MainMenu

    # Export a blueprint
    python ai_export_client.py export_blueprint /Game/Blueprints/BP_Player

    # List supported types
    python ai_export_client.py list_types

Port Discovery:
    The client automatically finds the server port by:
    1. Reading {ProjectDir}/Intermediate/MCTExport_port.txt
    2. Searching upward from current directory for port file
    3. Falling back to default port 55560
"""

import socket
import json
import sys
import os
from pathlib import Path

DEFAULT_PORT = 55560
PORT_RANGE = (55560, 55600)
TIMEOUT = 60  # seconds


def find_port_file_upward(start_dir=None):
    """Search upward from start_dir looking for Intermediate/MCTExport_port.txt"""
    current = Path(start_dir) if start_dir else Path.cwd()

    for _ in range(10):  # Limit search depth
        port_file = current / "Intermediate" / "MCTExport_port.txt"
        if port_file.exists():
            return port_file

        # Also check if we're inside plugin directory
        port_file = current.parent / "Intermediate" / "MCTExport_port.txt"
        if port_file.exists():
            return port_file

        parent = current.parent
        if parent == current:
            break
        current = parent

    return None


def get_port(project_dir=None):
    """
    Get the TCP server port.

    Priority:
    1. Explicit project_dir/Intermediate/MCTExport_port.txt
    2. Search upward from current directory
    3. Default port 55560
    """
    # 1. Check explicit project directory
    if project_dir:
        port_file = Path(project_dir) / "Intermediate" / "MCTExport_port.txt"
        if port_file.exists():
            try:
                return int(port_file.read_text().strip())
            except (ValueError, IOError):
                pass

    # 2. Search upward from current directory
    port_file = find_port_file_upward()
    if port_file:
        try:
            port = int(port_file.read_text().strip())
            print(f"[Port Discovery] Found port {port} from: {port_file}")
            return port
        except (ValueError, IOError):
            pass

    # 3. Default port
    print(f"[Port Discovery] Using default port {DEFAULT_PORT}")
    return DEFAULT_PORT


def check_port(port):
    """Check if a port is listening"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        result = s.connect_ex(('127.0.0.1', port))
        s.close()
        return result == 0
    except:
        return False


def get_mirrored_output_path(asset_path, base_output_dir):
    """Mirror asset's Content path in output directory.
    /Game/UI/W_Menu -> {base}/Game/UI/
    """
    relative = asset_path.lstrip("/").rsplit("/", 1)[0]
    return os.path.join(base_output_dir, relative)


def send_command(command_type, params=None, project_dir=None, port=None):
    """
    Send a command to the TCP server.

    Args:
        command_type: Command type (ping, export_widget, export_blueprint, list_supported_types)
        params: Optional parameters dict
        project_dir: Optional project directory for port discovery
        port: Optional explicit port (overrides discovery)

    Returns:
        dict: Server response
    """
    if port is None:
        port = get_port(project_dir)

    command = {"type": command_type}
    if params is not None:
        command["params"] = params

    json_command = json.dumps(command)

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT)
        s.connect(('127.0.0.1', port))
        s.sendall(json_command.encode('utf-8'))

        # Receive response
        response_data = b''
        while True:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                response_data += chunk
                # Try to parse JSON to check if complete
                try:
                    json.loads(response_data.decode('utf-8'))
                    break  # Valid JSON, we're done
                except json.JSONDecodeError:
                    continue  # Keep reading
            except socket.timeout:
                break

        s.close()

        if response_data:
            return json.loads(response_data.decode('utf-8'))
        else:
            return {"success": False, "error": "No response from server"}

    except ConnectionRefusedError:
        return {"success": False, "error": f"Connection refused on port {port}. Is Unreal Editor running?"}
    except socket.timeout:
        return {"success": False, "error": "Connection timed out"}
    except Exception as e:
        return {"success": False, "error": str(e)}


def cmd_ping(args):
    """Ping the server to check if it's running"""
    port = int(args[0]) if args else None
    response = send_command("ping", port=port)

    if response.get("success"):
        data = response.get("data", {})
        print(f"READY - {data.get('server', 'Unknown')} on port {data.get('port', 'unknown')}")
        return 0
    else:
        print(f"NOT READY - {response.get('error', 'Unknown error')}")
        return 1


def cmd_export_widget(args):
    """Export a widget blueprint"""
    if not args:
        print("Error: Missing asset path")
        print("Usage: ai_export_client.py export_widget /Game/UI/W_Widget")
        return 1

    asset_path = args[0]
    output_dir = args[1] if len(args) > 1 else ""

    # Auto-discover mirrored output directory if not specified
    if not output_dir:
        port_file = find_port_file_upward()
        if port_file:
            project_dir = port_file.parent.parent
            base_dir = str(project_dir / "Dev" / "AIExports")
        else:
            base_dir = "Dev/AIExports"
        output_dir = get_mirrored_output_path(asset_path, base_dir)

    params = {
        "asset_path": asset_path,
        "output_directory": output_dir,
        "both_formats": True
    }

    print(f"Exporting widget: {asset_path}")
    print(f"Output directory: {output_dir}")

    response = send_command("export_widget", params)

    if response.get("success"):
        data = response.get("data", {})
        print(f"\nExport successful!")
        print(f"  Asset: {data.get('asset_name')}")
        print(f"  Type: {data.get('asset_type')}")
        print(f"  Raw: {data.get('raw_file')}")
        print(f"  Simplified: {data.get('simplified_file')}")
        if data.get('stripped_file'):
            print(f"  Stripped: {data.get('stripped_file')}")
        return 0
    else:
        print(f"\nExport failed: {response.get('error')}")
        return 1


def cmd_export_blueprint(args):
    """Export a blueprint"""
    if not args:
        print("Error: Missing asset path")
        print("Usage: ai_export_client.py export_blueprint /Game/Blueprints/BP_Actor")
        return 1

    asset_path = args[0]
    output_dir = args[1] if len(args) > 1 else ""

    # Auto-discover mirrored output directory if not specified
    if not output_dir:
        port_file = find_port_file_upward()
        if port_file:
            project_dir = port_file.parent.parent
            base_dir = str(project_dir / "Dev" / "AIExports")
        else:
            base_dir = "Dev/AIExports"
        output_dir = get_mirrored_output_path(asset_path, base_dir)

    params = {
        "asset_path": asset_path,
        "output_directory": output_dir,
        "both_formats": True
    }

    print(f"Exporting blueprint: {asset_path}")
    print(f"Output directory: {output_dir}")

    response = send_command("export_blueprint", params)

    if response.get("success"):
        data = response.get("data", {})
        print(f"\nExport successful!")
        print(f"  Asset: {data.get('asset_name')}")
        print(f"  Type: {data.get('asset_type')}")
        print(f"  Raw: {data.get('raw_file')}")
        print(f"  Simplified: {data.get('simplified_file')}")
        if data.get('stripped_file'):
            print(f"  Stripped: {data.get('stripped_file')}")
        return 0
    else:
        print(f"\nExport failed: {response.get('error')}")
        return 1


def cmd_list_types(args):
    """List supported asset types"""
    response = send_command("list_supported_types")

    if response.get("success"):
        data = response.get("data", {})
        types = data.get("types", [])
        print("Supported asset types:")
        for t in types:
            print(f"  - {t}")
        return 0
    else:
        print(f"Error: {response.get('error')}")
        return 1


def cmd_list_commands(args):
    """List registered TCP commands"""
    response = send_command("list_commands")

    if response.get("success"):
        data = response.get("data", {})
        print(f"Registered commands ({data.get('count', 0)}):")
        for command in data.get("commands", []):
            params = "params" if command.get("requires_params") else "no params"
            mutating = "mutating" if command.get("mutating") else "read-only"
            print(f"  - {command.get('name')} [{command.get('category')}, {params}, {mutating}]")
        return 0
    else:
        print(f"Error: {response.get('error')}")
        return 1


def cmd_check(args):
    """Check if any server is running in port range"""
    print(f"Checking ports {PORT_RANGE[0]}-{PORT_RANGE[1]}...")

    for port in range(PORT_RANGE[0], PORT_RANGE[1] + 1):
        if check_port(port):
            print(f"  Port {port}: LISTENING")
            # Try to ping
            response = send_command("ping", port=port)
            if response.get("success"):
                data = response.get("data", {})
                print(f"    -> {data.get('server', 'Unknown')} ready")
        else:
            print(f"  Port {port}: not listening")

    return 0


def print_usage():
    """Print usage information"""
    print(__doc__)
    print("\nCommands:")
    print("  ping [port]                     - Check if server is running")
    print("  check                           - Scan port range for servers")
    print("  export_widget <path> [outdir]   - Export widget blueprint")
    print("  export_blueprint <path> [outdir] - Export blueprint")
    print("  list_types                      - List supported asset types")
    print("  list_commands                   - List registered TCP commands")
    print("\nExamples:")
    print("  python ai_export_client.py ping")
    print("  python ai_export_client.py export_widget /Game/UI/W_MainMenu")
    print("  python ai_export_client.py export_blueprint /Game/Blueprints/BP_Player")


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print_usage()
        return 1

    command = sys.argv[1].lower()
    args = sys.argv[2:]

    commands = {
        "ping": cmd_ping,
        "check": cmd_check,
        "export_widget": cmd_export_widget,
        "export_blueprint": cmd_export_blueprint,
        "list_commands": cmd_list_commands,
        "list_types": cmd_list_types,
        "list_supported_types": cmd_list_types,
    }

    if command in commands:
        return commands[command](args)
    else:
        print(f"Unknown command: {command}")
        print_usage()
        return 1


if __name__ == "__main__":
    sys.exit(main())
