#include "systemcalls.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/

    fflush(stdout); // Flush stdout to prevent printing twice

    int status = system(cmd);

    if(status == 0){
	// cmd returned success
	return true;
    }
    else{
	// cmd failed
	return false;
    }
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/

    fflush(stdout); // Flush stdout to prevent printing twice

    pid_t pid = fork();

    if(pid == 0){
	// child process
      	execv(command[0], command);

	// if code comes here means execv invocation failed
	_exit(1);
    }
    else if(pid > 0){
	// parent process
	int status;

	int waitpid_status = waitpid(pid, &status, 0);

	if(waitpid_status < 0){
	    // waitpid invocation failed
	    return false;
	}


	// waitpid returned ok
	// evaluate cmd execution status
	if(WIFEXITED(status)){
		// cmd exited normally
		
		if(WEXITSTATUS(status) != 0){
			// cmd exit code not 0
			return false;
		}
		else{
			// cmd exit code 0
			// cmd ran successfully
			return true;
		}

	}
	else{
		// cmd didn't exit normally
		// error in cmd exection

		return false;
	}
	
    }
    else{
	// pid < 0
	// fork invocation failed
	return false;
    }

    va_end(args);

    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    fflush(stdout); // Flush stdout to prevent printing twice
   
    pid_t pid = fork();

    if(pid == 0){
	// child process

	int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if(fd < 0){
		// file could not be opened
		_exit(1);
	}

	
	// Map stdout to file
	int dup_status = dup2(fd, STDOUT_FILENO);	// opened fd behave as STDOUT
	
	if(dup_status != STDOUT_FILENO){
		_exit(1);
	}

	// dont need fd anymore, so close
	close(fd);

	// Run exec - it should inherit dup property
      	execv(command[0], command);

	// if code comes here means execv invocation failed
	_exit(1);
    }
    else if(pid > 0){
	// parent process
	int cmd_run_status;

	
	int waitpid_status = waitpid(pid, &cmd_run_status, 0);

	if(waitpid_status < 0){
	    // waitpid invocation failed
	    return false;
	}


	// waitpid returned ok
	// evaluate cmd exectution status
	if(WIFEXITED(cmd_run_status)){
		// cmd exited normally
		if(WEXITSTATUS(cmd_run_status) != 0){
			// cmd exit code not 0
			return false;
		}
		else{
			// cmd exit code 0
			// cmd ran successfully
			return true;
		}

	}
	else{
		// cmd didn't exit normally
		// error in cmd exection

		return false;
	}
	
    }
    else{
	// pid < 0
	// fork invocation failed
	return false;
    }

    va_end(args);

    return true;
}
