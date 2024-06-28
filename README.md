# djsh (DJ Shell)
*Originally created March 2024*  
This program executes a limited linux shell.  
The shell can perform some basic built-in commands as well as execute path commands.  
Execute with ./djsh, which defaults to using execlp, or use ./djsh -execvp, to use execvp.  

Built-in commands:  
* exit:           exit djsh  
* cd <arg1>:      change directory (".." to go up a directory)  
* path:           print the current path variable  
* path <arg1>:    overwrite the path variable with colon-separated path directories  
* history:        print out recent inputs, up to 50  
* history <arg1>: specify the number of recent inputs to print  
