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
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include "tags.h"

typedef struct
{
	char * name;
	char * value;
}
ht_attr;

typedef struct
{
	int tag; // offset into tags[] (tags.h)
	int nattrs;
	ht_attr * attrs;
	int file; // not filled in by htparse, but other code might find it useful to have somewhere to store it
	int line; // line and column of the '<' that starts the opening-tag
	int col;
	int sib; // offset of adjacent elder sibling into ht_el array; -1 means :first-child
	int par; // offset of parent into ht_el array
}
ht_el;

// Interface strings and arguments for [f]printf()
#define USAGE_STRING	"Usage: csscover [-d] [-I=<importpath>] [-W[no-]<warning> [...]] <htmlfile> [...]"

#define PARSERR		"csscover: Error (Parser, state %d) at %d:%d, cstr '%s'\n"
#define PARSARG		state, line+1, pos+1, cstr

#define PARSEWARN	"csscover: warning: (Parser, state %d) at %d:%d\n"
#define PARSEWARG	state, line+1, pos+1

#define DPARSERR	"ERR:EPARSE:%d,%d.%d:"
#define DPARSARG	state, line, pos /* note, this is 0-based */

#define DPARSEWARN	"WARN:WPARSE:%d,%d.%d:"
#define DPARSEWARG	state, line, pos /* note, this is 0-based */

#define PMKLINE		"%.*s/* <- */%s\n", pos+1, lines[line], lines[line]+pos+1

// helper fn macros
#define max(a,b)	((a)>(b)?(a):(b))
#define min(a,b)	((a)<(b)?(a):(b))

// function protos
char * fgetl(FILE *); // gets a line of string data; returns a malloc-like pointer (preserves trailing \n)
char * getl(void); // like fgetl(stdin) but strips trailing \n
ht_el * htparse(char ** lines, int nlines, int * nels);
int push(char **string, int *length, char c);

// global vars
FILE *output;
bool daemonmode=false; // are we talking to another process? -d to set
bool trace=false; // for debugging, trace the parser's state and position
int nwarnings=0,maxwarnings=10;
bool wdtd=true;
bool wquoteattr=true;
bool wclose=true;

int main(int argc, char *argv[])
{
	output=stdout;
	int nfiles=0,cfiles=0;
	char ** filename=NULL; // HTML files to load
	char ** cssfiles=NULL; // CSS files to load
	char *importpath="";
	char ** h_assoc_ipath=NULL;
	char ** c_assoc_ipath=NULL;
	bool wdupfile=true;
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
		else if((strcmp(argt, "-t")==0)||(strcmp(argt, "--trace")==0))
		{
			trace=true;
			fprintf(output, "cssi: Tracing on stderr\n");
		}
		else if(strcmp(argt, "-Wall")==0) // TODO:generically handle warnings, so I don't have to remember to add each new warning to -Wall and -Wno-all
		{
			wdupfile=true;
			wdtd=true;
			wquoteattr=true;
			wclose=true;
		}
		else if(strcmp(argt, "-Wno-all")==0)
		{
			wdupfile=false;
			wdtd=false;
			wquoteattr=false;
			wclose=false;
		}
		else if(strcmp(argt, "-Wdupfile")==0)
		{
			wdupfile=true;
		}
		else if(strcmp(argt, "-Wno-dupfile")==0)
		{
			wdupfile=false;
		}
		else if(strcmp(argt, "-Wdtd")==0)
		{
			wdtd=true;
		}
		else if(strcmp(argt, "-Wno-dtd")==0)
		{
			wdtd=false;
		}
		else if(strcmp(argt, "-Wquoteattr")==0)
		{
			wquoteattr=true;
		}
		else if(strcmp(argt, "-Wno-quoteattr")==0)
		{
			wquoteattr=false;
		}
		else if(strcmp(argt, "-Wclose")==0)
		{
			wclose=true;
		}
		else if(strcmp(argt, "-Wno-close")==0)
		{
			wclose=false;
		}
		else if((strncmp(argt, "-w=", 3)==0)||(strncmp(argt, "--max-warn=", 11)==0))
		{
			sscanf(strchr(argt, '=')+1, "%d", &maxwarnings);
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
			h_assoc_ipath=(char **)realloc(h_assoc_ipath, nfiles*sizeof(char *));
			h_assoc_ipath[nfiles-1]=importpath;
		}
	}
	if(filename==NULL)
	{
		fprintf(output, "csscover: Error: No file given on command line!\n"USAGE_STRING"\n");
		if(daemonmode)
			printf("ERR:ENOFILE\n");
		return(1);
	}
	
	// read files and parse them find out what css files they need
	// we'll store all the parse results in RAM because we'll want to read them again later
	char ***mfile=(char ***)malloc(nfiles*sizeof(char**));
	if(!mfile)
	{
		fprintf(output, "csscover: Error: Failed to alloc mem for input files.\n");
		perror("malloc");
		if(daemonmode)
			printf("ERR:EMEM\n");
		return(2);
	}
	ht_el **html=(ht_el **)malloc(nfiles*sizeof(ht_el *));
	int file;
	int nlines[nfiles];
	int nels[nfiles];
	for(file=0;file<nfiles;file++)
	{
		fprintf(output, "csscover: reading %s\n", filename[file]);
		if(daemonmode)
			printf("READ:%s\n", filename[file]);
		mfile[file]=NULL;
		nlines[file]=0;
		html[file]=NULL;
		nels[file]=0;
		int j;
		for(j=0;j<file;j++)
		{
			if(strcmp(filename[file], filename[j])==0)
			{
				if(wdupfile && (nwarnings++<maxwarnings))
				{
					fprintf(output, "csscover: warning: Duplicate file in set: %s\n", filename[file]);
					if(daemonmode)
						printf("WARN:WDUPFILE:\"%s\"\n", filename[file]);
				}
				goto skip; // there is *nothing* *wrong* with the occasional goto
			}
		}
		FILE *fp;
		if(strcmp(filename[file], "-")==0)
			fp=stdin;
		else
			fp=fopen(filename[file], "r");
		if(!fp)
		{
			fprintf(output, "csscover: Error: Failed to open %s for reading!\n", filename[file]);
			if(daemonmode)
				printf("ERR:ECANTREAD:\"%s\"\n", filename[file]);
			return(2);
		}
		while(!feof(fp))
		{
			nlines[file]++;
			mfile[file]=(char **)realloc(mfile[file], nlines[file]*sizeof(char *));
			if(!mfile[file])
			{
				fprintf(output, "csscover: Error: Failed to alloc mem for input file.\n");
				perror("realloc");
				if(daemonmode)
					printf("ERR:EMEM\n");
				return(2);
			}
			mfile[file][nlines[file]-1]=fgetl(fp);
			if(!mfile[file][nlines[file]-1])
			{
				fprintf(output, "csscover: Error: Failed to alloc mem for input file.\n");
				perror("malloc/realloc");
				if(daemonmode)
					printf("ERR:EMEM\n");
				return(2);
			}
		}
		fclose(fp);
		while(mfile[file][nlines[file]-1][0]==0)
		{
			nlines[file]--;
			free(mfile[file][nlines[file]]);
		}
		html[file] = htparse(mfile[file], nlines[file], &nels[file]);
		int i;
		for(i=0;i<nels[file];i++)
		{
			if(trace)
				fprintf(stderr, "%d:%s\n", html[file][i].tag, tags[html[file][i].tag]);
			if(html[file][i].tag==TAG_LINK)
			{
				bool isss=false;
				int j;
				for(j=0;j<html[file][i].nattrs&&!isss;j++)
				{
					if(strcmp(html[file][i].attrs[j].name, "rel")==0)
					{
						if(strcmp(html[file][i].attrs[j].value, "stylesheet")==0)
						{
							isss=true;
						}
					}
				}
				if(isss)
				{
					for(j=0;j<html[file][i].nattrs;j++)
					{
						if(strcmp(html[file][i].attrs[j].name, "href")==0)
						{
							cfiles++;
							cssfiles=(char **)realloc(cssfiles, cfiles*sizeof(char *));
							if(html[file][i].attrs[j].value[0]=='/') // semi-absolute, so we use the ipath
							{
								cssfiles[cfiles-1]=(char *)malloc(strlen(h_assoc_ipath[file])+strlen(html[file][i].attrs[j].value)+1);
								sprintf(cssfiles[cfiles-1], "%s%s", h_assoc_ipath[file], html[file][i].attrs[j].value+1);
							}
							else // relative, so we use the path to the htmlfile
							{
								char * cpath = strdup(filename[file]);
								while(!((cpath[strlen(cpath)-1]=='/')||(cpath[strlen(cpath)-1]==0)))
									cpath[strlen(cpath)-1]=0;
								cssfiles[cfiles-1]=(char *)malloc(strlen(cpath)+strlen(html[file][i].attrs[j].value)+1);
								sprintf(cssfiles[cfiles-1], "%s%s", cpath, html[file][i].attrs[j].value);
								free(cpath);
							}
							c_assoc_ipath=(char **)realloc(c_assoc_ipath, cfiles*sizeof(char *));
							c_assoc_ipath[cfiles-1]=h_assoc_ipath[file];
						}
					}
				}
			}
		}
		skip:
		;
	}
	fprintf(output, "csscover: input files read\n");
	if(daemonmode)
		printf("READ*\n");
		
	int wp[2],rp[2],e;
	int ww,rr;
	if((e=pipe(wp)))
	{
		fprintf(output, "csscover: Error: Failed to create pipe: %s\n", strerror(errno));
		if(daemonmode)
			printf("ERR:EPIPE\n");
		return(2);
	}
	if((e=pipe(rp)))
	{
		fprintf(output, "csscover: Error: Failed to create pipe: %s\n", strerror(errno));
		if(daemonmode)
			printf("ERR:EPIPE\n");
		return(2);
	}
	int pid=fork();
	switch(pid)
	{
		case -1: // failure
			fprintf(output, "csscover: Error: failed to fork cssi: %s\n", strerror(errno));
			if(daemonmode)
			{
				printf("ERR:EFORK\n");
			}
		break;
		case 0: // child
			{
				ww=rp[1];close(rp[0]);
				rr=wp[0];close(wp[1]);
				if(!trace)
					close(STDERR_FILENO); // we don't want to see cssi's long-form messages
				if((e=dup2(ww, STDOUT_FILENO))==-1)
				{
					fprintf(stderr, "csscover.child: Error: Failed to redirect stdout with dup2: %s\n", strerror(errno));
					write(rp[1], "ERR:EDUP2\n", strlen("ERR:EDUP2\n"));
					return(2);
				}
				if((e=dup2(rr, STDIN_FILENO))==-1)
				{
					fprintf(stderr, "csscover.child: Error: Failed to redirect stdin with dup2: %s\n", strerror(errno));
					write(rp[1], "ERR:EDUP2\n", strlen("ERR:EDUP2\n"));
					return(2);
				}
				char *eargv[3+2*cfiles];
				eargv[0]="cssi";
				eargv[1]="-d";
				if(trace)
					fprintf(stderr, "execvp(\"cssi\", {\"cssi\", \"-d\"");
				int i;
				for(i=0;i<cfiles;i++)
				{
					eargv[2*i+2]=(char *)malloc(4+strlen(c_assoc_ipath[i]));
					sprintf(eargv[2*i+2], "-I=%s", c_assoc_ipath[i]);
					eargv[2*i+3]=cssfiles[i];
					if(trace)
						fprintf(stderr, ", \"%s\", \"%s\"", eargv[2*i+2], eargv[2*i+3]);
				}
				eargv[2+2*cfiles]=NULL;
				if(trace)
					fprintf(stderr, ", NULL})\n");
				execvp("cssi", eargv);
				fprintf(stderr, "csscover.child: Error: Failed to execvp cssi: %s\n", strerror(errno));
				write(rp[1], "ERR:EEXEC\n", strlen("ERR:EEXEC\n"));
				return(255);
			}
		break;
		default: // parent
			ww=wp[1];close(wp[0]);
			rr=rp[0];close(rp[1]);
		break;
	}
	fd_set master, readfds;
	FD_SET(STDIN_FILENO, &master);
	FD_SET(rr, &master);
	int nfds=max(rr, STDIN_FILENO)+1;
	int errupt=0;
	fprintf(output, "csscover>");
	fflush(output);
	while(!errupt)
	{
		readfds=master;
		int e=select(nfds, &readfds, NULL, NULL, NULL); // blocks until something happens
		if(e==-1)
		{
			fprintf(output, "csscover: Error: select() failed: %s\n", strerror(errno));
			if(daemonmode)
				printf("ERR:ESELECT\n");
			return(2);
		}
		if(FD_ISSET(STDIN_FILENO, &readfds))
		{
			// orders from above
			char * input=getl();
			char * cmd=strtok(input, " ");
			int parmc=0; // the names are, of course, modelled on argc and argv.  TODO: pipelines (will require considerable encapsulation)
			char ** parmv=NULL;
			char *p;
			while((p=strtok(NULL, " ")))
			{
				parmc++;
				parmv=(char **)realloc(parmv, parmc*sizeof(char *));
				parmv[parmc-1]=p;
			}
			if(cmd)
			{
				if(strncmp(cmd, "quit", strlen(cmd))==0) // quit
				{
					errupt++;
				}
				else
				{
					if(daemonmode)
						printf("ERR:EBADCMD:\"%s\"\n", cmd);
					else
						fprintf(output, "cssi: Error: unrecognised command %s!\n", cmd);
				}
			}
			if(parmv)
				free(parmv);
			if(input)
				free(input);
		}
		if(FD_ISSET(rr, &readfds))
		{
			// message from below
		}
	}
	return(0);
}

/* WARNING, this fgetl() is not like my usual getl(); this one keeps the \n */
// gets a line of string data, {re}alloc()ing as it goes, so you don't need to make a buffer for it, nor must thee fret thyself about overruns!
char * fgetl(FILE *fp)
{
	char * lout = (char *)malloc(81);
	int i=0;
	signed int c;
	while(!feof(fp))
	{
		c=fgetc(fp);
		if(c==EOF) // EOF without '\n' - we'd better put an '\n' in
			c='\n';
		if(c!=0)
		{
			lout[i++]=c;
			if((i%80)==0)
			{
				if((lout=(char *)realloc(lout, i+81))==NULL)
				{
					printf("\nNot enough memory to store input!\n");
					free(lout);
					return(NULL);
				}
			}
		}
		if(c=='\n') // we do want to keep them this time
			break;
	}
	lout[i]=0;
	char *nlout=(char *)realloc(lout, i+1);
	if(nlout==NULL)
	{
		return(lout); // it doesn't really matter (assuming realloc is a decent implementation and hasn't nuked the original pointer), we'll just have to temporarily waste a bit of memory
	}
	return(nlout);
}

char * getl(void)
{
	// gets a line of string data, {re}alloc()ing as it goes, so you don't need to make a buffer for it, nor must thee fret thyself about overruns!
	char * lout = (char *)malloc(81);
	int i=0;
	char c;
	while(1)
	{
		c = getchar();
		if (c == 10)
			break;
		if (c != 0)
		{
			lout[i++]=c;
			if ((i%80) == 0)
			{
				if ((lout = (char *)realloc(lout, i+81))==NULL)
				{
					printf("\nNot enough memory to store input!\n");
					free(lout);
					return(NULL);
				}
			}
		}
	}
	lout[i]=0;
	char *nlout=(char *)realloc(lout, i+1);
	if(nlout==NULL)
	{
		return(lout); // it doesn't really matter (assuming realloc is a decent implementation and hasn't nuked the original pointer), we'll just have to temporarily waste a bit of memory
	}
	return(nlout);
}

// TODO on error we should ht_free(rv) and return(NULL), instead of return(rv)ing
ht_el * htparse(char ** lines, int nlines, int * nels)
{
	*nels=0;
	ht_el * rv=NULL;
	int line=0;
	int pos=0;
	int state=0;
	bool igwhite=true;
	char *curr;
	char *cstr=NULL;
	int cstl=0;
	int dtds=0;
	int no_dtd_w=0;
	int parent=-1;
	char *attr;
	bool quot=false;
	bool closer=false;
	ht_el blank={-1, 0, NULL, -1, -1, -1, -1, -1}, htop=blank;
	while(line<nlines)
	{
		curr=&lines[line][pos];
		if(trace)
			fprintf(stderr, "%d\t%d:%d\t%hhu\t'%c'\n", state, line+1, pos+1, *curr, *curr);
		if(igwhite && strchr(" \t\r\f\n", *curr))
		{
			pos++;
		}
		else
		{
			switch(state)
			{
				case 0: // looking for a '<'
					igwhite=true;
					htop=blank;
					closer=false;
					cstr=NULL;
					cstl=0;
					if(*curr=='<')
					{
						state=1;
						htop.line=line;
						htop.col=pos;
						htop.par=parent;
						if(htop.par>=0)
						{
							int i;
							for(i=(*nels)-1;i>=0;i--)
							{
								if(rv[i].par==parent)
								{
									htop.sib=i;
									break;
								}
							}
						}
					}
					pos++;
				break;
				case 1: // check for '<!' TODO: and '</'
					if(*curr=='!')
						state=2;
					else
					{
						if(*curr=='/')
							closer=true;
						if(wdtd && (dtds==0) && !no_dtd_w)
						{
							fprintf(output, PARSEWARN"\tNo <!DOCTYPE> declared or not first element\n", PARSEWARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSEWARN"no <!DOCTYPE> or not first element\n", DPARSEWARG);
							no_dtd_w=true;
						}
						state=3;
					}
					igwhite=false; // things we're interested in are delimited by whitespace
					if(!closer && push(&cstr, &cstl, *curr))
					{
						fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
						fprintf(output, PMKLINE);
						if(daemonmode)
							printf(DPARSERR"out of memory\n", DPARSARG);
						if(cstr) free(cstr);
						return(rv);
					}
					pos++;
				break;
				case 2: // SGML/XML <!declaration>
					if(cstr && (strcmp("!--", cstr)==0)) // technically this is wrong, we should have a separate parser for <!declarations> which interprets -- as comment delimiter; but almost all major browsers and most HTML authors are lazy about correct SGML/XML comment syntax, so we will be too - after all, css-tools is not a validator
					{
						state=9;
						free(cstr);
						cstr=NULL;
						cstl=0;
						pos++;
					}
					else if(strchr(" [", *curr))
					{
						state=4;
					}
					else
					{
						if(push(&cstr, &cstl, *curr))
						{
							fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"out of memory\n", DPARSARG);
							if(cstr) free(cstr);
							return(rv);
						}
						pos++;
					}
				break;
				case 3:
					if(strchr(" >", *curr))
					{
						int i;
						for(i=0;i<ntags;i++)
						{
							if(strcmp(cstr, tags[i])==0)
								break;
						}
						if(i<ntags)
						{
							htop.tag=i;
							if(*curr=='>')
							{
								if(closer)
								{
									if(parent>=0)
									{
										if(i==rv[parent].tag)
										{
											parent=rv[parent].par;
										}
										else
										{ // this might not really be an error, since in HTML you're allowed to do this; in XHTML it's an error
											fprintf(output, PARSERR"\tIncorrect nesting or closed tag not open\n", PARSARG);
											fprintf(output, PMKLINE);
											if(daemonmode)
												printf(DPARSERR"incorrect nesting\n", DPARSARG);
											if(cstr) free(cstr);
											return(rv);
										}
									}
									else
									{
										fprintf(output, PARSERR"\tIncorrect nesting or closed tag not open\n", PARSARG);
										fprintf(output, PMKLINE);
										if(daemonmode)
											printf(DPARSERR"incorrect nesting\n", DPARSARG);
										if(cstr) free(cstr);
										return(rv);
									}
									state=0;
								}
								else
								{
									// element finished, add it to rv
									(*nels)++;
									ht_el * new=(ht_el *)realloc(rv, (*nels)*sizeof(ht_el));
									if(new)
									{
										rv=new;
										new[(*nels)-1]=htop;
										parent=(*nels)-1;
										state=0;
									}
									else
									{
										fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
										fprintf(output, PMKLINE);
										if(daemonmode)
											printf(DPARSERR"out of memory\n", DPARSARG);
										if(cstr) free(cstr);
										return(rv);
									}
								}
							}
							else
							{
								igwhite=true; // eat up whitespace until the attr starts
								state=6;
							}
						}
						else
						{
							fprintf(output, PARSERR"\tUnrecognised element name\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"unrecognised element name\n", DPARSARG);
							if(cstr) free(cstr);
							return(rv);
						}
						if(cstr) free(cstr);
						cstr=NULL;
						cstl=0;
					}
					else
					{
						if(push(&cstr, &cstl, *curr))
						{
							fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"out of memory\n", DPARSARG);
							if(cstr) free(cstr);
							return(rv);
						}
					}
					pos++;
				break;
				case 4:
					if(strcmp(cstr, "!DOCTYPE")==0)
					{
						// We ignore <!DOCTYPE> because our parser isn't clever enough to care - it just assumes XHTML and doesn't validate
						state=5;
					}
					else
					{
						fprintf(output, PARSERR"\tUnrecognised <!declaration>\n", PARSARG);
						fprintf(output, PMKLINE);
						if(daemonmode)
							printf(DPARSERR"unrecognised <!declaration>\n", DPARSARG);
						return(rv);
						if(cstr) free(cstr);
						cstr=NULL;
						cstl=0;
					}
				break;
				case 5:
					// We also assume that the document has an external DTD, so we can just scan for a '>'
					if(*curr=='>')
					{
						if((dtds==1) && wdtd && (nwarnings++ < maxwarnings))
						{
							fprintf(output, PARSEWARN"\tMultiple <!DOCTYPE> declarations\n", PARSEWARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSEWARN"multiple <!DOCTYPE>\n", DPARSEWARG);
						}
						if(cstr) free(cstr);
						cstr=NULL;
						cstl=0;
						dtds++;
						state=0;
					}
					// But just in case, if we hit a '[' we throw a warning, -Wdtd
					if((*curr=='[') && wdtd && (nwarnings++ < maxwarnings))
					{
						fprintf(output, PARSEWARN"\t<!DOCTYPE> contains [, may be internal\n", PARSEWARG);
						fprintf(output, PMKLINE);
						if(daemonmode)
							printf(DPARSEWARN"<!DOCTYPE> contains [, may be internal\n", DPARSEWARG);
					}
					pos++;
				break;
				case 6: // attribute name; scanning for '=' TODO: whitespace is permitted both sides of the '='
					igwhite=false;
					if(strchr(" \t\r\f\n", *curr))
					{
						igwhite=true;
						state=12;
					}
					else if(*curr=='=')
					{
						igwhite=true;
						attr=cstr;
						cstr=NULL;
						cstl=0;
						state=7;
					}
					else if(*curr=='>')
					{
						bool close=false;
						if(cstr) // can't have an attr without a value
						{
							if(strcmp(cstr, "/")==0) // '/>' closes a tag; it's not an attr
							{
								free(cstr);
								cstr=NULL;
								close=true;
							}
							else
							{
								fprintf(output, PARSERR"\tMalformed attribute\n", PARSARG);
								fprintf(output, PMKLINE);
								if(daemonmode)
									printf(DPARSERR"malformed attribute\n", DPARSARG);
								if(cstr) free(cstr);
								return(rv);
							}
						}
						if(closer)
						{
							fprintf(output, PARSERR"\tAttributes not allowed in closing tags\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"attributes not allowed in closing tags\n", DPARSARG);
							if(cstr) free(cstr);
							return(rv);
						}
						else
						{
							// element finished, add it to rv
							(*nels)++;
							ht_el * new=(ht_el *)realloc(rv, (*nels)*sizeof(ht_el));
							if(new)
							{
								rv=new;
								new[(*nels)-1]=htop;
								if(!close) // for a self-closing tag, the parent is the same as it was before
									parent=(*nels)-1;
								state=0;
							}
							else
							{
								fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
								fprintf(output, PMKLINE);
								if(daemonmode)
									printf(DPARSERR"out of memory\n", DPARSARG);
								return(rv);
							}
						}
					}
					else
					{
						if(push(&cstr, &cstl, *curr))
						{
							fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"out of memory\n", DPARSARG);
							if(cstr) free(cstr);
							return(rv);
						}
					}
					pos++;
				break;
				case 7: // attribute value, check for "
					igwhite=false;
					if(*curr=='"')
					{
						quot=true;
						pos++;
					}
					else
					{
						quot=false;
						if(wquoteattr && (nwarnings++ < maxwarnings))
						{
							fprintf(output, PARSEWARN"\tUnquoted attribute value\n", PARSEWARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSEWARN"unquoted attribute value\n", DPARSEWARG);
						}
					}
					state=8;
				break;
				case 8: // attribute value, scanning for quot?closing ":whitespace
					if(!quot && strchr(" \t\r\f\n", *curr))
					{
						fprintf(output, PARSERR"\tWhitespace in unquoted attribute value\n", PARSARG);
						fprintf(output, PMKLINE);
						if(daemonmode)
							printf(DPARSERR"whitespace in unquoted attribute value\n", DPARSARG);
						if(cstr) free(cstr);
						if(attr) free(attr);
						return(rv);
					}
					else if(*curr=='"')
					{
						if(quot)
						{
							htop.nattrs++;
							ht_attr * new=(ht_attr *)realloc(htop.attrs, htop.nattrs*sizeof(ht_attr));
							if(new)
							{
								htop.attrs=new;
								new[htop.nattrs-1].name=attr;
								new[htop.nattrs-1].value=cstr;
								attr=NULL;
								cstr=NULL;
								cstl=0;
								igwhite=true;
								state=6;
								pos++;
							}
							else
							{
								fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
								fprintf(output, PMKLINE);
								if(daemonmode)
									printf(DPARSERR"out of memory\n", DPARSARG);
								if(attr) free(attr);
								if(cstr) free(cstr);
								return(rv);
							}
						}
						else
						{
							fprintf(output, PARSERR"\t\" in unquoted attribute value\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"\" in unquoted attribute value\n", DPARSARG);
							return(rv);
						}
					}
					else if(!quot && strchr(" \t\r\f\n>", *curr))
					{
						htop.nattrs++;
						ht_attr * new=(ht_attr *)realloc(htop.attrs, htop.nattrs*sizeof(ht_attr));
						if(new)
						{
							htop.attrs=new;
							new[htop.nattrs-1].name=attr;
							new[htop.nattrs-1].value=cstr;
							attr=NULL;
							cstr=NULL;
							cstl=0;
							igwhite=true;
							state=6;
							if(*curr!='>')
								pos++;
						}
						else
						{
							fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"out of memory\n", DPARSARG);
							if(attr) free(attr);
							if(cstr) free(cstr);
							return(rv);
						}
					}
					else
					{
						if(push(&cstr, &cstl, *curr))
						{
							fprintf(output, PARSERR"\tOut of memory\n", PARSARG);
							fprintf(output, PMKLINE);
							if(daemonmode)
								printf(DPARSERR"out of memory\n", DPARSARG);
							if(cstr) free(cstr);
							if(attr) free(attr);
							return(rv);
						}
						pos++;
					}
				break;
				case 9: // states 9,10,11 are doing lazy & (technically) incorrect comment handling
					if(*curr=='-')
						state=10;
					pos++;
				break;
				case 10:
					if(*curr=='-')
						state=11;
					else
						state=9;
					pos++;
				break;
				case 11:
					if(*curr=='>')
						state=0;
					else
						state=9;
					pos++;
				break;
				case 12:
					if(*curr=='=')
					{
						igwhite=true;
						attr=cstr;
						cstr=NULL;
						cstl=0;
						state=7;
						pos++;
					}
					else
					{
						fprintf(output, PARSERR"\tWhitespace in attribute name\n", PARSARG);
						fprintf(output, PMKLINE);
						if(daemonmode)
							printf(DPARSERR"whitespace in attribute name\n", DPARSARG);
						if(cstr) free(cstr);
						return(rv);
					}
				break;
				default:
					fprintf(output, PARSERR"\tNo such state!\n", PARSARG);
					fprintf(output, PMKLINE);
					if(daemonmode)
						printf(DPARSERR"no such state\n", DPARSARG);
					return(rv);
				break;
			}
		}
		if(*curr=='\n') // handle newlines TODO: catch instances of newlines occuring where whitespace is significant or disallowed (although css-tools *is not a validator*)
		{
			line++;
			pos=0;
		}
	}
	if((parent!=-1) && wclose && (nwarnings++ < maxwarnings))
	{
		fprintf(output, PARSEWARN"\tElement not closed at EOF: %s\n", PARSEWARG, tags[rv[parent].tag]);
		if(daemonmode)
			printf(DPARSEWARN"element not closed at EOF:\"%s\"\n", DPARSEWARG, tags[rv[parent].tag]);
	}
	return(rv);
}

int push(char **string, int *length, char c)
{
	char *new=(char *)realloc(*string, (*length)+2);
	if(new)
	{
		*string=new;
		new[(*length)++]=c;
		new[(*length)]=0;
		return(0);
	}
	return(1);
}
