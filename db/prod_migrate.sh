#!/bin/bash

set -e
# save current directory
current_dir=$(pwd)
# enter the parent directory of where the script is located
cd "$(dirname "$0")/.."
# run the migration script
echo "Running production migration script..."
if [ ! -f db/.env_prod_local ]; then
  echo "Error: db/.env_prod_local file not found!"
  exit 1
fi
# Ensure dbmate is installed and available in PATH
if ! command -v dbmate &> /dev/null; then
  echo "Error: dbmate is not installed or not in PATH."
  exit 1
fi
# Run dbmate with the specified environment file
dbmate --env-file db/.env_prod_local migrate


