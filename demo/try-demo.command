#!/bin/sh
# Runs CMS Portal with the sample accounts and records in the demo-data folder.
cd "$(dirname "$0")" || exit 1
./cms --data demo-data
