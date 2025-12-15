#!/usr/bin/env python3
"""
Test script for MERGE functionality using UDP directly
"""
import socket
import struct
import time
import sys

# Message structure matching C code
def create_message(ordre, emetteur, texte, logo="", groupe=""):
    """Create ISYMessage matching C struct"""
    msg = bytearray(164)  # Total size of ISYMessage
    
    # ordre[4] - bytes 0-3
    ordre_bytes = ordre.encode()[:4]
    msg[0:len(ordre_bytes)] = ordre_bytes
    
    # emetteur[20] - bytes 4-23
    emetteur_bytes = emetteur.encode()[:20]
    msg[4:4+len(emetteur_bytes)] = emetteur_bytes
    
    # emoji[8] - bytes 24-31
    logo_bytes = logo.encode()[:8]
    msg[24:24+len(logo_bytes)] = logo_bytes
    
    # groupe[32] - bytes 32-63
    groupe_bytes = groupe.encode()[:32]
    msg[32:32+len(groupe_bytes)] = groupe_bytes
    
    # texte[100] - bytes 64-163
    texte_bytes = texte.encode()[:100]
    msg[64:64+len(texte_bytes)] = texte_bytes
    
    return bytes(msg)

def send_command(server_ip, server_port, username, command):
    """Send command to server"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    msg = create_message("CMD", username, command)
    
    print(f"[{username}] -> {command}")
    sock.sendto(msg, (server_ip, server_port))
    
    # Wait for reply (optional)
    sock.settimeout(1.0)
    try:
        data, _ = sock.recvfrom(164)
        # Extract reply text (starting at byte 64, which is texte field)
        reply_text = data[64:].decode().rstrip('\x00')
        if reply_text:
            print(f"[SERVER] <- {reply_text}")
    except socket.timeout:
        print("[SERVER] <- (no reply)")
    
    sock.close()
    time.sleep(0.5)

# Test
if __name__ == "__main__":
    SERVER_IP = "127.0.0.1"  # Use loopback to test locally
    SERVER_PORT = 8000
    
    print("=== MERGE Test with Direct UDP ===\n")
    
    # Test 1: Create GroupA
    print("--- Step 1: Create GroupA ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "CREATE GroupA")
    
    # Test 2: alice joins GroupA
    print("\n--- Step 2: alice joins GroupA ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "JOIN GroupA")
    time.sleep(1)  # Give server time to create group process
    
    # Test 3: bob joins GroupA
    print("\n--- Step 3: bob joins GroupA ---")
    send_command(SERVER_IP, SERVER_PORT, "bob", "JOIN GroupA")
    time.sleep(1)
    
    # Test 4: Create GroupB
    print("\n--- Step 4: Create GroupB ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "CREATE GroupB")
    
    # Test 5: alice joins GroupB
    print("\n--- Step 5: alice joins GroupB ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "JOIN GroupB")
    time.sleep(1)
    
    # Test 6: Check files before merge
    print("\n--- Files BEFORE merge ---")
    import subprocess
    subprocess.run(["ls", "-la", "infoGroup/"])
    
    # Test 7: List GroupA
    print("\n--- GroupA members before merge ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "LISTMEMBER GroupA")
    
    # Test 8: List GroupB
    print("\n--- GroupB members before merge ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "LISTMEMBER GroupB")
    
    # Test 9: MERGE
    print("\n--- Merging GroupA -> GroupB ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "MERGE GroupA GroupB")
    time.sleep(1)
    
    # Test 10: Check files after merge
    print("\n--- Files AFTER merge ---")
    subprocess.run(["ls", "-la", "infoGroup/"])
    
    # Test 11: Check GroupB file contents
    print("\n--- GroupB.txt contents ---")
    try:
        with open("infoGroup/GroupB.txt", "r") as f:
            for line in f:
                print(f"  {line.rstrip()}")
    except FileNotFoundError:
        print("  (file not found)")
    
    # Test 12: List GroupB after merge
    print("\n--- GroupB members after merge ---")
    send_command(SERVER_IP, SERVER_PORT, "alice", "LISTMEMBER GroupB")
    
    print("\n=== Test complete ===")
