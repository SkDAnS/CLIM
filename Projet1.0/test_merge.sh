#!/bin/bash

# Test script for MERGE functionality
# Expected: Create GroupA (with moderator+user), Create GroupB (moderator only),
# Merge GroupA->GroupB, verify all members appear in GroupB

SERVER_IP="10.148.111.54"
SERVER_PORT="8000"

# Helper function to send command to server
send_cmd() {
    local username="$1"
    local command="$2"
    
    echo "[$username] Sending: $command"
    (echo "$username"; echo "$command") | nc -q 1 "$SERVER_IP" "$SERVER_PORT" 2>/dev/null
    sleep 0.5
}

# Clean start
echo "=== Starting MERGE test ==="
sleep 1

# Test 1: Moderator creates GroupA
echo ""
echo "--- Test 1: Moderator (alice) creates GroupA ---"
send_cmd "alice" "CREATE GroupA"

# Test 2: Moderator joins GroupA
echo ""
echo "--- Test 2: Moderator (alice) joins GroupA ---"
send_cmd "alice" "JOIN GroupA"

# Test 3: Another user (bob) joins GroupA
echo ""
echo "--- Test 3: User (bob) joins GroupA ---"
send_cmd "bob" "JOIN GroupA"

# Test 4: List GroupA members (should show alice + bob)
echo ""
echo "--- Test 4: List GroupA members ---"
send_cmd "alice" "LISTMEMBER GroupA"

# Test 5: Moderator creates GroupB
echo ""
echo "--- Test 5: Moderator (alice) creates GroupB ---"
send_cmd "alice" "CREATE GroupB"

# Test 6: Moderator joins GroupB
echo ""
echo "--- Test 6: Moderator (alice) joins GroupB ---"
send_cmd "alice" "JOIN GroupB"

# Test 7: List GroupB members (should show only alice)
echo ""
echo "--- Test 7: List GroupB members BEFORE merge ---"
send_cmd "alice" "LISTMEMBER GroupB"

# Test 8: Merge GroupA into GroupB
echo ""
echo "--- Test 8: MERGE GroupA GroupB ---"
send_cmd "alice" "MERGE GroupA GroupB"

# Test 9: Check files after merge
echo ""
echo "--- Test 9: Check infoGroup files after merge ---"
ls -la infoGroup/

# Test 10: List GroupB members (should show alice + bob)
echo ""
echo "--- Test 10: List GroupB members AFTER merge ---"
send_cmd "alice" "LISTMEMBER GroupB"

# Test 11: Verify GroupA is gone
echo ""
echo "--- Test 11: Try to list GroupA (should fail) ---"
send_cmd "alice" "LISTMEMBER GroupA"

echo ""
echo "=== Test complete ==="
