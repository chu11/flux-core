#!/bin/sh -e
# client exit before service.add response doesn't crash broker
cat <<EOF >service.add.lua
#!/usr/bin/env lua
local f = require 'flux' .new()
f:send ("service.add", { service = "foo" })
EOF
chmod +x service.add.lua

# Run service.add 3x and ensure no broker crash
STATEDIR=$(mktemp -d)
flux start -o,-Sstatedir=${STATEDIR} sh -c './service.add.lua; ./service.add.lua; ./service.add.lua'
