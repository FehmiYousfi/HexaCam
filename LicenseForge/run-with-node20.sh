#!/bin/bash
# Helper script to run npm commands with Node.js 20

export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
nvm use 20
exec "$@"
