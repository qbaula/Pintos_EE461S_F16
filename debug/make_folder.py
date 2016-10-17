#!/usr/bin/python

import subprocess
import sys
import shutil

if len(sys.argv) > 1:
    project = sys.argv[1]
    if ((project == "userprog") or (project == "vm")
       or (project == "threads") or (project == "filesys")):

        # construct gcc directives array
        num_args = len(sys.argv) - 2
        directives = "" 
        for i in range (0, num_args):
            directives += " -D debug" + sys.argv[i + 2]

        # project name good, get path to project folder
        project_folder = "../../" + project
        
        # make copy of Make.vars file to restore after writing
        shutil.copy(project_folder + "/Make.vars", "./")        

        # edit DEFINES in Make.vars to include directives in gcc
        Make_vars = open(project_folder + "/Make.vars", "rw+")
        line_num = 0
        line_str = 0
        for i, line in enumerate(Make_vars):
            # tokenize line
            tokens = line.split()
            # look for indicator of DEFINES line
            if len(tokens) > 0:
                if tokens[0] == "kernel.bin:":
                    line_num = i 
                    line_str = line
                    break
        
        # insert the directives array into the end of this line
        # find where to insert directives array
        Make_vars.seek(0);
        for i in range (0, line_num):
            Make_vars.readline()
        Make_vars.seek(len(line_str) - 1, 1)
        # pos is where the directives array is inserted
        pos = Make_vars.tell()
        # save remainer of the file
        Make_vars.seek(1, 1)
        lines = Make_vars.readlines()
        # insert directives array 
        Make_vars.seek(pos)
        Make_vars.write(directives + "\n")
        # restore remaineder of file
        Make_vars.writelines(lines)
        Make_vars.close()

        # touch all files in project directory so make will recompile new directives
        # does not work with a subprocess call to touch directly, must use bash script
        subprocess.call("./touchall.sh")

        # re-complie project directory with new directives
        # again this must be completed with a bash script, a direct subprocess call does not work
        subprocess.call("./recompile.sh")

        # copy back original Make.vars file so pintos state is preserved
        shutil.copy("Make.vars", project_folder)

























