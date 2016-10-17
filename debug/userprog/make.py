#!/usr/bin/python 

import subprocess
import sys

args = ["../make_folder.py", "userprog"]

# remove first arg from command line and call project folder compiler script
if len(sys.argv) > 1:
    for i in range (1, len(sys.argv)):
        args.append(sys.argv[i]) 

subprocess.call(args)
