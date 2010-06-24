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
	
	cssi - understand your nightmare CSS codebase
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

//#include "tags.h" // not currently used

// Interface strings and arguments for [f]printf()
#define USAGE_STRING	"Usage: cssi [-d][-t] [-I=<importpath>] [-W[no-]newline] <filename> [...]"

#define PARSERR		"cssi: Error (Parser, state %d) at %d:%d\n"
#define PARSARG		state, line+1, pos+1

#define PARSEWARN	"cssi: warning: (Parser, state %d) at %d:%d\n"
#define PARSEWARG	state, line+1, pos+1

#define DPARSERR	"ERR:EPARSE:%d,%d.%d:"
#define DPARSARG	state, line, pos /* note, this is 0-based */

#define DPARSEWARN	"WARN:WPARSE:%d,%d.%d:"
#define DPARSEWARG	state, line, pos /* note, this is 0-based */

#define PMKLINE		"%.*s/* <- */%s\n", pos+1, mfile[line], mfile[line]+pos+1

// structs for representing CSS things

typedef struct
{
	int nmatches;
	char ** matches; // the selectors
	char * innercode; // what's between the braces (not that we intend to parse this yet)
	signed int file; // which file is it in? (0-based); -1 means 'not yet filled in'
	signed int line; // line number on which the selector appears (0-based!!!); -1 means 'not yet filled in'
	int numlines; // how far to the closing brace? (0==closing brace is on same line as selector)
}
entry;

typedef struct
{
	char * text; // we aren't parsing this yet
	int ent; // index into entries table
}
selector;

// function protos
char * getl(FILE *); // gets a line of string data; returns a malloc-like pointer
selector * mergesort(selector * array, int len);

int main(int argc, char *argv[])
{
	/*unsigned char version_maj, version_min, version_rev;
	sscanf(VERSION, "%hhu.%hhu.%hhu", &version_maj, &version_min, &version_rev);*/
	int nfiles=0;
	char ** filename=NULL; // files to load
	char *importpath="";
	bool daemon=false; // are we talking to another process?
	bool trace=false; // for debugging, trace the parser's state and position
	bool wnewline=true;
	bool wdupfile=true;
	int maxwarnings=10;
	int arg;
	for(arg=1;arg<argc;arg++)
	{
		char *argt=argv[arg];
		if((strcmp(argt, "-h")==0)||(strcmp(argt, "--help")==0))
		{
			fprintf(stderr, USAGE_STRING"\n");
			return(0);
		}
		else if((strcmp(argt, "-d")==0)||(strcmp(argt, "--daemon")==0))
		{
			daemon=true;
			fprintf(stderr, "cssi: Daemon mode is active.\n");
			printf("CSSI:%s\n", VERSION);//%hhu.%hhu.%hhu\n", version_maj, version_min, version_rev);
		}
		else if((strcmp(argt, "-t")==0)||(strcmp(argt, "--trace")==0))
		{
			trace=true;
			fprintf(stderr, "cssi: Tracing on stderr\n");
		}
		else if(strcmp(argt, "-Wall")==0)
		{
			wnewline=true;
			wdupfile=true;
			// ALL of two warnings!
		}
		else if(strcmp(argt, "-Wno-all")==0)
		{
			wnewline=false;
			wdupfile=false;
			// ALL of two warnings!
		}
		else if(strcmp(argt, "-Wnewline")==0)
		{
			wnewline=true;
		}
		else if(strcmp(argt, "-Wno-newline")==0)
		{
			wnewline=false;
		}
		else if(strcmp(argt, "-Wdupfile")==0)
		{
			wdupfile=true;
		}
		else if(strcmp(argt, "-Wno-dupfile")==0)
		{
			wdupfile=false;
		}
		else if(strncmp(argt, "-I=", 3)==0)
		{
			importpath=argt+3;
		}
		else if((strncmp(argt, "-m=", 3)==0)||(strncmp(argt, "--max-warn=", 11)==0))
		{
			sscanf(strchr(argt, '=')+1, "%d", &maxwarnings);
		}
		else
		{
			// assume it's a filename
			nfiles++;
			filename=(char **)realloc(filename, nfiles*sizeof(char *));
			filename[nfiles-1]=argt;
		}
	}
	if(filename==NULL)
	{
		fprintf(stderr, "cssi: Error: No file given on command line!\n"USAGE_STRING"\n");
		if(daemon)
			printf("ERR:ENOFILE:No file given on command line!\n");
		return(1);
	}
	int i;
	int nwarnings=0;
	int nentries=0;
	int initnfiles=nfiles; // initial nfiles, so we know if we've been @imported
	entry * entries=NULL;
	for(i=0;i<nfiles;i++)
	{
		FILE *fp=NULL;
		int j;
		for(j=0;j<i;j++)
		{
			if(strcmp(filename[i], filename[j])==0)
			{
				if(wdupfile && (nwarnings++<maxwarnings))
				{
					fprintf(stderr, "cssi: warning: Duplicate file in set%s: %s\n", i<initnfiles?"":" (from @import)", filename[i]);
					if(daemon)
						printf("WARN:WDUPFILE:%d:%s\n", i<initnfiles?0:1, filename[i]);
				}
				goto skip; // there is *nothing* *wrong* with the occasional goto
			}
		}
		if(strcmp(filename[i], "-")==0)
			fp=stdin;
		else
			fp=fopen(filename[i], "r");
		if(!fp)
		{
			fprintf(stderr, "cssi: Error: Failed to open %s for reading!\n", i==nfiles?"<stdin>":filename[i]);
			if(daemon)
				printf("ERR:ECANTREAD:Failed to open %s for reading!\n", i==nfiles?"<stdin>":filename[i]);
			return(1);
		}
		char ** mfile=NULL;
		int nlines=0;
		while(!feof(fp))
		{
			nlines++;
			mfile=(char **)realloc(mfile, nlines*sizeof(char *));
			mfile[nlines-1]=getl(fp);
		}
		fclose(fp);
		while(mfile[nlines-1][0]==0)
		{
			nlines--;
		}
		
		fprintf(stderr, "cssi: processing %s\n", i==nfiles?"<stdin>":filename[i]);
		if(daemon)
			printf("PROC:%s\n", i==nfiles?"<stdin>":filename[i]); // Warning; it is possible to have a file named '<stdin>', though unlikely
	
		// Parse it with a state machine
		int state=0;
		int ostate=0; // for parentheticals, e.g. comments.  Doesn't handle nesting - we'd need a stack for that - but comments can't be nested anyway
		int line=0;
		int pos=0;
		bool whitespace[]={true, true, false}; // eat up whitespace?
		bool nonl=false;
		const entry eblank={0, NULL, NULL, -1, -1, 0};
		entry current=eblank;
		int curstrlen=0;
		char * curstring=NULL;
		while(line<nlines)
		{
			char *curr=mfile[line]+pos;
			if(trace)
				fprintf(stderr, "%d\t%d:%d\t%c\n", state, line+1, pos+1, *curr);
			if(*curr==0)
			{
				if(line==nlines-1)
				{
					line++;
				}
				else
				{
					fprintf(stderr, PARSERR"\tUnexpected EOL\n", PARSARG);
					fprintf(stderr, PMKLINE);
					if(daemon)
						printf(DPARSERR"unexpected EOL\n", DPARSARG);
					return(2);
				}
			}
			else if((*curr=='/') && (*(curr+1)=='*')) // /* comment */
			{
				ostate=state;
				state=1;
				pos+=2;
			}
			else if(whitespace[state]&&(*curr=='\n'))
			{
				if((state==0)&&curstring)
				{
					curstrlen++;
					curstring=(char *)realloc(curstring, curstrlen);
					curstring[curstrlen-1]=*curr;
				}
				free(mfile[line]);
				line++;
				pos=0;
				nonl=false;
			}
			else if(whitespace[state]&&((*curr==' ')||(*curr=='\t')))
			{
				if((state==0)&&curstring)
				{
					curstrlen++;
					curstring=(char *)realloc(curstring, curstrlen+1);
					curstring[curstrlen-1]=*curr;
					curstring[curstrlen]=0;
				}
				pos++;
			}
			else
			{
				switch(state)
				{
					case 0: // selector, comma, or braces
						if(nonl && wnewline && (nwarnings++<maxwarnings))
						{
							fprintf(stderr, PARSEWARN"\tMissing newline between entries\n", PARSEWARG);
							fprintf(stderr, PMKLINE);
							if(daemon)
								printf(DPARSEWARN"missing newline between entries\n", DPARSEWARG);
						}
						if(*curr==';')
						{
							pos++;
						}
						else if(*curr=='@')
						{
							if(pos!=0 && (nwarnings++<maxwarnings))
							{
								fprintf(stderr, PARSEWARN"\tAt-rule not at start of line\n", PARSEWARG);
								fprintf(stderr, PMKLINE);
								if(daemon)
									printf(DPARSEWARN"at-rule not at start of line\n", DPARSEWARG);
							}
							if(strncmp(curr, "@import", strlen("@import"))==0)
							{
								//TODO check it's early enough not to be ignored by the UA
								// Also, this code isn't robust at all
								// and we should probably be using something similar to the innercode curstring stuff, but I cba
								char *url=strchr(curr, '(');
								if(!url)
								{
									fprintf(stderr, PARSERR"\tMalformed @import directive\n", PARSARG);
									fprintf(stderr, PMKLINE);
									if(daemon)
										printf(DPARSERR"malformed @import directive\n", DPARSARG);
									return(2);
								}
								url++;
								char *endurl=strchr(url, ')');
								if(!endurl)
								{
									fprintf(stderr, PARSERR"\tMalformed @import directive\n", PARSARG);
									fprintf(stderr, PMKLINE);
									if(daemon)
										printf(DPARSERR"malformed @import directive\n", DPARSARG);
									return(2);
								}
								*endurl=0;
								pos=(endurl-mfile[line])+1;
								nfiles++;
								filename=(char **)realloc(filename, nfiles*sizeof(char *));
								filename[nfiles-1]=(char *)malloc(strlen(importpath)+strlen(url));
								sprintf(filename[nfiles-1], "%s%s", importpath, url);
							}
							else
							{
								fprintf(stderr, PARSERR"\tUnrecognised at-rule\n", PARSARG);
								fprintf(stderr, PMKLINE);
								if(daemon)
									printf(DPARSERR"unrecognised at-rule\n", DPARSARG);
								return(2);
							}
						}
						else
						{
							current.line=line;
							current.file=i;
							if(*curr==',')
							{
								if(curstring)
								{
									current.nmatches++;
									current.matches=(char **)realloc(current.matches, current.nmatches*sizeof(char *));
									current.matches[current.nmatches-1]=curstring;
									curstring=NULL; // disconnect the pointer as its target is finished
									curstrlen=0;
								}
								else
								{
									fprintf(stderr, PARSERR"\tEmpty selector before comma\n", PARSARG);
									fprintf(stderr, PMKLINE);
									if(daemon)
										printf(DPARSERR"empty selector before comma\n", DPARSARG);
									return(2);
								}
								pos++;
							}
							else if(*curr=='{')
							{
								if(!curstring)
								{
									fprintf(stderr, PARSERR"\tEmpty selector before decl\n", PARSARG);
									fprintf(stderr, PMKLINE);
									if(daemon)
										printf(DPARSERR"empty selector before decl\n", DPARSARG);
									return(2);
								}
								current.nmatches++;
								current.matches=(char **)realloc(current.matches, current.nmatches*sizeof(char *));
								current.matches[current.nmatches-1]=curstring;
								curstring=NULL; // disconnect the pointer as its target is finished
								curstrlen=0;
								state=2;
								pos++;
							}
							else
							{
								curstrlen++;
								curstring=(char *)realloc(curstring, curstrlen+1);
								curstring[curstrlen-1]=*curr;
								curstring[curstrlen]=0;
								pos++;
							}
						}
					break;
					case 1: // comment */
						if((*curr=='*') && (*(curr+1)=='/'))
						{
							state=ostate;
							pos+=2;
						}
						else
							pos++;
					break;
					case 2: // 'innercode \}
						if(*curr=='}')
						{
							current.innercode=curstring;
							curstring=NULL; // disconnect the pointer as its target is finished
							curstrlen=0;
							current.numlines=line-current.line;
							nentries++;
							entries=(entry *)realloc(entries, nentries*sizeof(entry));
							entries[nentries-1]=current;
							current=eblank;
							pos++;
							state=0;
							nonl=true;
						}
						else
						{
							curstrlen++;
							curstring=(char *)realloc(curstring, curstrlen+1);
							curstring[curstrlen-1]=*curr;
							curstring[curstrlen]=0;
							if(*curr=='\n')
							{
								line++;
								pos=0;
							}
							else
							{
								pos++;
							}
						}
					break;
					default:
						fprintf(stderr, PARSERR"\tNo such state!\n", PARSARG);
						fprintf(stderr, PMKLINE);
						if(daemon)
							printf(DPARSERR"no such state\n", DPARSARG);
						return(2);
					break;
				}
			}
		}
		free(mfile);
		fprintf(stderr, "cssi: parsed %s\n", i==nfiles?"<stdin>":filename[i]);
		if(daemon)
			printf("PARSED:%s\n", i==nfiles?"<stdin>":filename[i]); // Warning; it is possible to have a file named '<stdin>', though unlikely
		skip:;
	}
	fprintf(stderr, "cssi: Parsing completed\n");
	if(daemon)
		printf("PARSED*\n");
	if(nwarnings>maxwarnings)
	{
		fprintf(stderr, "cssi: warning: %d more warnings were not displayed.\n", nwarnings-maxwarnings);
		if(daemon)
			printf("XSWARN:%d\n", nwarnings-maxwarnings);
	}
	
	fprintf(stderr, "cssi: collating selectors\n");
	if(daemon)
		printf("COLL\n");
	int nsels=0;
	selector * sels=NULL;
	for(i=0;i<nentries;i++)
	{
		int j;
		for(j=0;j<entries[i].nmatches;j++)
		{
			nsels++;
			sels=(selector *)realloc(sels, nsels*sizeof(selector));
			sels[nsels-1].text=entries[i].matches[j];
			char *txt=sels[nsels-1].text;
			while(strlen(txt) && (txt[strlen(txt)-1]==' ')) // strip trailing whitespace
				txt[strlen(txt)-1]=0;
			sels[nsels-1].ent=i;
		}
	}
	selector * sort=mergesort(sels, nsels);
	for(i=0;i<nsels;i++)
	{
		int ent=sort[i].ent;
		int file=entries[ent].file;
		fprintf(stderr, "In %s at %d:\t%s\n", file<nfiles?filename[file]:"<stdin>", entries[ent].line+1, sort[i].text);
	}
	return(0);
}

/* WARNING, this is not the same as my usual getl(); this one keeps the \n */
// gets a line of string data, {re}alloc()ing as it goes, so you don't need to make a buffer for it, nor must thee fret thyself about overruns!
char * getl(FILE *fp)
{
	char * lout = (char *)malloc(81);
	int i=0;
	signed int c;
	while(!feof(fp))
	{
		c=fgetc(fp);
		if(c==EOF)
			break;
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

selector * mergesort(selector * array, int len)
{
	if(len<1)
		return(NULL);
	selector *rv=(selector *)malloc(len*sizeof(selector));
	switch(len)
	{
		case 1:
			rv[0]=array[0];
			return(rv);
		break;
		case 2:
			if(strcmp(array[0].text, array[1].text)<=0)
			{
				rv[0]=array[0];
				rv[1]=array[1];
			}
			else
			{
				rv[0]=array[1];
				rv[1]=array[0];
			}
			return(rv);
		break;
		default:
			;
			int i=len/2;
			selector *left=(selector *)malloc(i*sizeof(selector));
			selector *right=(selector *)malloc((len-i)*sizeof(selector));
			int j;
			for(j=0;j<len;j++)
			{
				if(j<i)
					left[j]=array[j];
				else
					right[j-i]=array[j];
			}
			left=mergesort(left, i);
			right=mergesort(right, len-i);
			int p=0,q=0;
			for(j=0;j<len;j++)
			{
				if(p==i)
				{
					rv[j]=right[q++];
				}
				else if(q==len-i)
				{
					rv[j]=left[p++];
				}
				else
				{
					if(strcmp(left[p].text, right[q].text)<=0)
					{
						rv[j]=left[p++];
					}
					else
					{
						rv[j]=right[q++];
					}
				}
			}
			free(left);
			free(right);
			return(rv);
		break;
	}
}
