/*
	css-tools - make sense of your CSS
	Copyright (C) 2010 Edward Cree

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	
	csscover - connect CSS and HTML
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "tags.h"

// Interface strings and arguments for [f]printf()
#define USAGE_STRING	"Usage: csscover [-d] [-I=<importpath>] [-W[no-]<warning> [...]] <filename> [...]"

// helper fn macros
#define max(a,b)	((a)>(b)?(a):(b))
#define min(a,b)	((a)<(b)?(a):(b))

// global vars
FILE *output;
bool daemonmode=false; // are we talking to another process? -d to set

int main(int argc, char *argv[])
{
	output=stdout;
	int nfiles=0;
	char ** filename=NULL; // files to load
	char *importpath="";
	char ** assoc_ipath=NULL;
	int arg;
	for(arg=1;arg<argc;arg++)
	{
		char *argt=argv[arg];
		if((strcmp(argt, "-h")==0)||(strcmp(argt, "--help")==0))
		{
			fprintf(output, USAGE_STRING"\n");
			return(0);
		}
		else if((strcmp(argt, "-d")==0)||(strcmp(argt, "--daemon")==0))
		{
			daemonmode=true;
			output=stderr;
			fprintf(output, "csscover: Daemon mode is active.\n");
			printf("CSSCOVER:\"%s\"\n", VERSION);
		}
		else if(strncmp(argt, "-I=", 3)==0)
		{
			importpath=argt+3;
			if(importpath[strlen(importpath)-1]!='/')
			{
				char *p=(char *)malloc(strlen(importpath)+2);
				sprintf(p, "%s/", importpath);
				importpath=p; // technically this leads to a memory leak, since importpath never gets free()d - but since you're not likely to give more than a few -I= options, it shouldn't matter
			}
		}
		else
		{
			// assume it's a filename
			nfiles++;
			filename=(char **)realloc(filename, nfiles*sizeof(char *));
			filename[nfiles-1]=argt;
			assoc_ipath=(char **)realloc(assoc_ipath, nfiles*sizeof(char *));
			assoc_ipath[nfiles-1]=importpath;
		}
	}
	if(filename==NULL)
	{
		fprintf(output, "csscover: Error: No file given on command line!\n"USAGE_STRING"\n");
		if(daemonmode)
			printf("ERR:ENOFILE\n");
		return(1);
	}
	return(0);
}
