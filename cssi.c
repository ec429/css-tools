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

#include "tags.h"

// helper fn macros
#define max(a,b)	((a)>(b)?(a):(b))
#define min(a,b)	((a)<(b)?(a):(b))

// Interface strings and arguments for [f]printf()
#define USAGE_STRING	"Usage: cssi [-d][-t] [-I=<importpath>] [-W[no-]<warning> [...]] <filename> [...]"

#define PARSERR		"cssi: Error (Parser, state %d) at %d:%d\n"
#define PARSARG		state, line+1, pos+1

#define PARSEWARN	"cssi: warning: (Parser, state %d) at %d:%d\n"
#define PARSEWARG	state, line+1, pos+1

#define DPARSERR	"ERR:EPARSE:%d,%d.%d:"
#define DPARSARG	state, line, pos /* note, this is 0-based */

#define DPARSEWARN	"WARN:WPARSE:%d,%d.%d:"
#define DPARSEWARG	state, line, pos /* note, this is 0-based */

#define PMKLINE		"%.*s/* <- */%s\n", pos+1, mfile[line], mfile[line]+pos+1

#define SPARSERR	"cssi: Error (Sel-Parser, state %d) SelId %d, col %d\n"
#define SPARSARG	state, sid, pos+1

#define SPARSEWARN	"cssi: warning: (Sel-Parser, state %d) SelId %d, col %d\n"
#define SPARSEWARG	state, sid, pos+1

#define DSPARSERR	"ERR:ESPARSE:%d,%d.%d:"
#define DSPARSARG	state, sid, pos /* note, this is 0-based */

#define DSPARSEWARN	"WARN:WSPARSE:%d,%d.%d:"
#define DSPARSEWARG	state, sid, pos /* note, this is 0-based */

#define SPMKLINE	"%.*s/* <- */%s\n", pos+1, s.text, s.text+pos+1

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

typedef enum
{
	DESC,	// Descendant ("E F")
	CHLD,	// Child ("E > F")
	SBLG,	// Sibling ("E + F")
	SELF	// own properties, like "E.f", "E:f" etc.
}
family;

typedef enum
{
	NONE, // 'unknown', for error condition
	UNIV,
	TAG,
	CLASS,
	PCLASS,
	ID,
	ATTR
}
seltype;

typedef struct _sel_elt
{
	seltype type;
	char * data; // eg "#foo" becomes type=ID, data="foo"; "[bar]" becomes type=ATTR, data="bar".
	family nextrel;
	struct _sel_elt * next; // linked-list
}
sel_elt;

typedef struct
{
	char * text; // we aren't parsing this yet
	sel_elt * chain; // but when we do it'll go here
	int ent; // index into entries table
}
selector;

// function protos
char * fgetl(FILE *); // gets a line of string data; returns a malloc-like pointer (preserves trailing \n)
char * getl(char *); // like fgetl(stdin) but prints a prompt too (strips trailing \n)
selector * selmergesort(selector * array, int len);
int parse_selector(selector, int);
void tree_free(sel_elt * node);

// global vars
FILE *output;
bool daemon=false; // are we talking to another process? -d to set

int main(int argc, char *argv[])
{
	/*unsigned char version_maj, version_min, version_rev;
	sscanf(VERSION, "%hhu.%hhu.%hhu", &version_maj, &version_min, &version_rev);*/
	output=stdout;
	int nfiles=0;
	char ** filename=NULL; // files to load
	char *importpath="";
	bool trace=false; // for debugging, trace the parser's state and position
	bool wnewline=true;
	bool wdupfile=true;
	bool watrule=true;
	int maxwarnings=10;
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
			daemon=true;
			output=stderr;
			fprintf(output, "cssi: Daemon mode is active.\n");
			printf("CSSI:\"%s\"\n", VERSION);//%hhu.%hhu.%hhu\n", version_maj, version_min, version_rev);
		}
		else if((strcmp(argt, "-t")==0)||(strcmp(argt, "--trace")==0))
		{
			trace=true;
			fprintf(output, "cssi: Tracing on stderr\n");
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
		else if(strcmp(argt, "-Watrule")==0)
		{
			watrule=true;
		}
		else if(strcmp(argt, "-Wno-atrule")==0)
		{
			watrule=false;
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
		fprintf(output, "cssi: Error: No file given on command line!\n"USAGE_STRING"\n");
		if(daemon)
			printf("ERR:ENOFILE\n");
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
					fprintf(output, "cssi: warning: Duplicate file in set%s: %s\n", i<initnfiles?"":" (from @import)", filename[i]);
					if(daemon)
						printf("WARN:WDUPFILE:%d:\"%s\"\n", i<initnfiles?0:1, filename[i]);
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
			fprintf(output, "cssi: Error: Failed to open %s for reading!\n", i==nfiles?"<stdin>":filename[i]);
			if(daemon)
				printf("ERR:ECANTREAD:\"%s\"\n", i==nfiles?"<stdin>":filename[i]);
			return(1);
		}
		char ** mfile=NULL;
		int nlines=0;
		while(!feof(fp))
		{
			nlines++;
			mfile=(char **)realloc(mfile, nlines*sizeof(char *));
			if(!mfile)
			{
				fprintf(output, "cssi: Error: Failed to alloc mem for input file.\n");
				perror("malloc/realloc");
				if(daemon)
					printf("ERR:EMEM\n");
				return(1);
			}
			mfile[nlines-1]=fgetl(fp);
			if(!mfile[nlines-1])
			{
				fprintf(output, "cssi: Error: Failed to alloc mem for input file.\n");
				perror("malloc/realloc");
				if(daemon)
					printf("ERR:EMEM\n");
				return(1);
			}
		}
		fclose(fp);
		while(mfile[nlines-1][0]==0)
		{
			nlines--;
			free(mfile[nlines]);
		}
		
		fprintf(output, "cssi: processing %s\n", i==nfiles?"<stdin>":filename[i]);
		if(daemon)
			printf("PROC:\"%s\"\n", i==nfiles?"<stdin>":filename[i]); // Warning; it is possible to have a file named '<stdin>', though unlikely
	
		// Parse it with a state machine
		int state=0;
		int ostate=0; // for parentheticals, e.g. comments.  Doesn't handle nesting - we'd need a stack for that - but comments can't be nested anyway
		int line=0;
		int pos=0;
		int brace=0;
		bool whitespace[]={true, true, false, true, true}; // eat up whitespace?
		bool nonl=false;
		const entry eblank={0, NULL, NULL, -1, -1, 0};
		entry current=eblank;
		int curstrlen=0;
		char * curstring=NULL;
		while(line<nlines)
		{
			char *curr=mfile[line]+pos;
			if(trace)
				fprintf(output, "%d\t%d:%d\t%hhu\t'%c'\n", state, line+1, pos+1, *curr, *curr);
			if(*curr==0)
			{
				if(line==nlines-1)
				{
					line++;
				}
				else
				{
					fprintf(output, PARSERR"\tUnexpected EOL\n", PARSARG);
					fprintf(output, PMKLINE);
					if(daemon)
						printf(DPARSERR"unexpected EOL\n", DPARSARG);
					return(2);
				}
			}
			else if((*curr=='/') && (*(curr+1)=='*') && (state!=1)) // /* comment */
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
					curstring=(char *)realloc(curstring, curstrlen+1);
					curstring[curstrlen-1]=*curr;
					curstring[curstrlen]=0;
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
							fprintf(output, PARSEWARN"\tMissing newline between entries\n", PARSEWARG);
							fprintf(output, PMKLINE);
							if(daemon)
								printf(DPARSEWARN"missing newline between entries\n", DPARSEWARG);
						}
						if(*curr==';')
						{
							pos++;
						}
						else if(*curr=='@')
						{
							if(((pos!=0) || current.nmatches) && watrule && (nwarnings++<maxwarnings))
							{
								fprintf(output, PARSEWARN"\tAt-rule not at start of line\n", PARSEWARG);
								fprintf(output, PMKLINE);
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
									fprintf(output, PARSERR"\tMalformed @import directive\n", PARSARG);
									fprintf(output, PMKLINE);
									if(daemon)
										printf(DPARSERR"malformed @import directive\n", DPARSARG);
									return(2);
								}
								url++;
								char *endurl=strchr(url, ')');
								if(!endurl)
								{
									fprintf(output, PARSERR"\tMalformed @import directive\n", PARSARG);
									fprintf(output, PMKLINE);
									if(daemon)
										printf(DPARSERR"malformed @import directive\n", DPARSARG);
									return(2);
								}
								*endurl=0;
								pos=(endurl-mfile[line])+1;
								nfiles++;
								filename=(char **)realloc(filename, nfiles*sizeof(char *));
								filename[nfiles-1]=(char *)malloc(strlen(importpath)+strlen(url)+1);
								sprintf(filename[nfiles-1], "%s%s", importpath, url);
							}
							else if(strncmp(curr, "@media", strlen("@media"))==0)
							{
								pos+=strlen("@media");
								state=3; // ignore @media, just find the block close
							}
							else
							{
								fprintf(output, PARSERR"\tUnrecognised at-rule\n", PARSARG);
								fprintf(output, PMKLINE);
								if(daemon)
									printf(DPARSERR"unrecognised at-rule\n", DPARSARG);
								return(2);
							}
						}
						else
						{
							if(current.line==-1)
							{
								current.line=line;
								current.file=i;
							}
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
									fprintf(output, PARSERR"\tEmpty selector before comma\n", PARSARG);
									fprintf(output, PMKLINE);
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
									fprintf(output, PARSERR"\tEmpty selector before decl\n", PARSARG);
									fprintf(output, PMKLINE);
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
								brace=1;
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
						if(*curr=='{')
						{
							brace++;
						}
						else if(*curr=='}')
						{
							brace--;
							if(brace==0)
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
						}
						if(state==2) // not closed the last brace yet, so keep adding to the string
						{
							curstrlen++;
							curstring=(char *)realloc(curstring, curstrlen+1);
							curstring[curstrlen-1]=*curr;
							curstring[curstrlen]=0;
							if(*curr=='\n')
							{
								free(mfile[line]);
								line++;
								pos=0;
							}
							else
							{
								pos++;
							}
						}
					break;
					case 3:
						if(*curr==';')
						{
							state=0;
						}
						else if(*curr=='{')
						{
							state=4;
							brace=1;
						}
						pos++;
					break;
					case 4:
						if(*curr=='{')
						{
							brace++;
						}
						else if(*curr=='}')
						{
							brace--;
							if(brace==0)
								state=0;
						}
						pos++;
					break;
					default:
						fprintf(output, PARSERR"\tNo such state!\n", PARSARG);
						fprintf(output, PMKLINE);
						if(daemon)
							printf(DPARSERR"no such state\n", DPARSARG);
						return(2);
					break;
				}
			}
		}
		free(mfile);
		fprintf(output, "cssi: parsed %s\n", i==nfiles?"<stdin>":filename[i]);
		if(daemon)
			printf("PARSED:\"%s\"\n", i==nfiles?"<stdin>":filename[i]); // Warning; it is possible to have a file named '<stdin>', though unlikely
		skip:;
	}
	fprintf(output, "cssi: Parsing completed\n");
	if(daemon)
		printf("PARSED*\n");
	if(nwarnings>maxwarnings)
	{
		fprintf(output, "cssi: warning: %d more warnings were not displayed.\n", nwarnings-maxwarnings);
		if(daemon)
			printf("XSWARN:%d\n", nwarnings-maxwarnings);
	}
	
	fprintf(output, "cssi: collating & parsing selectors\n");
	if(daemon)
		printf("COLL:\n");
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
			sels[nsels-1].chain=NULL;
		}
	}
	selector * sort=selmergesort(sels, nsels);
	
	int nerrs=0;
	for(i=0;i<nsels;i++)
	{
		int e;
		if((e=parse_selector(sort[i], i))) // assigns & tests NZ
		{
			nerrs++;
		}
	}
	
	fprintf(output, "cssi: collated & parsed selectors\n");
	if(nerrs)
		fprintf(output, "cssi:  there were %d errors.\n", nerrs);
	if(daemon)
		printf("COLL*:%d\n", nerrs);
	
	int errupt=0;
	while(!errupt)
	{
		char * input=getl("cssi>");
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
		if(strncmp(cmd, "selector", strlen(cmd))==0) // selectors
		{
			if(daemon)
				printf("SEL...\n"); // line ending with '...' indicates "continue until a line is '.'"
			else
				fprintf(output, "cssi: matching SELECTORS\n");
			for(i=0;i<nsels;i++)
			{
				bool show=true;
				int ent=sort[i].ent;
				int file=entries[ent].file;
				int parm;
				for(parm=0;(parm<parmc)&&show;parm++)
				{
					char *sparm=strdup(parmv[parm]);
					char *cmp=sparm;
					while(*cmp && !strchr("=<>:", *cmp))
						cmp++;
					if(!*cmp)
					{
						if(daemon)
							printf("ERR:EBADPARM:NOCOMP:%d:\"%s\"\n", parm, parmv[parm]);
						else
							fprintf(output, "cssi: Error: Bad matcher %s (no comparator found)\n", parmv[parm]);
						i=nsels;show=false;break;
					}
					char wcmp=*cmp;
					*cmp=0;
					cmp++;
					bool num=false;
					bool tree=false;
					char *smatch=NULL;
					int nmatch;
					if(strcmp(sparm, "sid")==0)
					{
						num=true;nmatch=i;
					}
					else if(strcmp(sparm, "file")==0)
					{
						smatch=filename[file];
					}
					else if(strcmp(sparm, "line")==0)
					{
						num=true;nmatch=entries[ent].line+(daemon?0:1); // daemon mode uses 0-based linenos
					}
					else if(strcmp(sparm, "match")==0)
					{
						tree=true;
						smatch=(char *)sort[i].chain; // it's a sel_ent *, really, not a char *
					}
					else
					{
						if(daemon)
							printf("ERR:EBADPARM:BADPARAM:%d:\"%s\"\n", parm, parmv[parm]);
						else
							fprintf(output, "cssi: Error: Bad matcher %s (unrecognised param)\n", parmv[parm]);
						i=nsels;show=false;break;
					}
					int inval=0;
					if(num)
					{
						sscanf(cmp, "%d", &inval);
					}
					switch(wcmp)
					{
						case '=':
							if(tree)
							{
								if(daemon)
									printf("ERR:ENOSYS:TREEMATCH:%d:\"%s\"\n", parm, parmv[parm]);
								else
									fprintf(output, "cssi: Error: tree-matching unimplemented (%s)\n", parmv[parm]);
								i=nsels;show=false;break;
							}
							else if(num)
							{
								show&=(nmatch == inval);
							}
							else
							{
								show&=(strcmp(smatch, cmp)==0);
							}
						break;
						case '<':
							if(num)
							{
								show&=(nmatch < inval);
							}
							else
							{
								if(daemon)
									printf("ERR:EBADPARM:NUMCOMP:%d:\"%s\"\n", parm, parmv[parm]);
								else
									fprintf(output, "cssi: Error: '<' is for numerics only (%s)\n", parmv[parm]);
								i=nsels;show=false;break;
							}
						break;
						case '>':
							if(num)
							{
								show&=(nmatch > inval);
							}
							else
							{
								if(daemon)
									printf("ERR:EBADPARM:NUMCOMP:%d:\"%s\"\n", parm, parmv[parm]);
								else
									fprintf(output, "cssi: Error: '>' is for numerics only (%s)\n", parmv[parm]);
								i=nsels;show=false;break;
							}
						break;
						default: // this should be impossible
							if(daemon)
								printf("ERR:EBADPARM:BADCOMP:%d:\"%s\"\n", parm, parmv[parm]);
							else
								fprintf(output, "cssi: Error: Bad matcher %s (bad comparator)\n", parmv[parm]);
							i=nsels;show=false;break;
						break;
					}
					free(sparm);
				}
				if(show)
				{
					if(daemon)
						printf("RECORD:ID=\"%d\":FILE=\"%s\":LINE=\"%d\":SEL=\"%s\"\n", i, file<nfiles?filename[file]:"<stdin>", entries[ent].line+1, sort[i].text);
					else
						fprintf(output, "%d\tIn %s at %d:\t%s\n", i, file<nfiles?filename[file]:"<stdin>", entries[ent].line+1, sort[i].text);
				}
			}
			if(daemon)
				printf(".\n");
		}
		else if(strncmp(cmd, "quit", strlen(cmd))==0) // quit
		{
			// no params yet
			errupt++;
		}
		else
		{
			if(daemon)
				printf("ERR:EBADCMD:\"%s\"\n", cmd);
			else
				fprintf(output, "cssi: Error: unrecognised command %s!\n", cmd);
		}
		if(parmv)
			free(parmv);
		free(input);
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

char * getl(char * prompt)
{
	printf("%s", prompt);
	fflush(stdout);
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

selector * selmergesort(selector * array, int len)
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
			left=selmergesort(left, i);
			right=selmergesort(right, len-i);
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

int parse_selector(selector s, int sid)
{
	s.chain=NULL; // initially empty
	// state machine
	int state=0;
	int pos=0;
	char *curr;
	char *cstr=NULL;
	int cstl=0;
	family nextrel=SELF;
	sel_elt * node=s.chain;
	seltype type=NONE;
	bool igwhite=false;
	while(*(curr=s.text+pos) || state) // assigns curr to the current position, then checks the char there is not '\0' - if it is, and state=0, then stop
	{
		switch(state)
		{
			case 0: // get an identifier
				if(*curr=='.')
				{
					pos++;
					state=1;
					type=CLASS;
					cstr=NULL;cstl=0;
				}
				else if(*curr==':')
				{
					pos++;
					state=1;
					type=PCLASS;
					cstr=NULL;cstl=0;
				}
				else if(*curr=='#')
				{
					pos++;
					state=1;
					type=ID;
					cstr=NULL;cstl=0;
				}
				else if(*curr=='*')
				{
					type=UNIV;
					cstr=NULL;
					state=2;
					pos++;
				}
				else if(strchr(" \t\n\r\f", *curr)) // whitespace (though \n should /not/ happen)
				{
					if(!igwhite)
						nextrel=DESC;
					pos++;
				}
				else if(*curr=='>')
				{
					nextrel=CHLD;
					pos++;
					igwhite=true; // ' > ' is like '>', not ' '.
				}
				else if(*curr=='+')
				{
					nextrel=SBLG;
					pos++;
					igwhite=true; // ' + ' is like '+', not ' '.
				}
				else
				{
					type=NONE; // don't know - it might be a tag but we can't check till we've read the whole string
					state=1;
				}
			break;
			case 1: // read a string until the next identifier-delimiter
				if(strchr(":.#[ >+", *curr) || !*curr) // is it time to end the string?
				{
					cstr=(char *)realloc(cstr, ++cstl);
					cstr[cstl-1]=0;
					state=2;
				}
				else
				{
					cstr=(char *)realloc(cstr, ++cstl+1);
					cstr[cstl-1]=*curr;
					cstr[cstl]=0;
					pos++;
				}
			break;
			case 2: // have read name
				if(type==NONE)
				{
					if(!(cstr && cstr[0]))
					{
						fprintf(output, SPARSERR"\tEmpty selent\n", SPARSARG);
						fprintf(output, SPMKLINE);
						if(daemon)
							printf(DSPARSERR"empty selent\n", DSPARSARG);
						tree_free(s.chain);
						return(1);
					}
					int i;
					for(i=0;i<ntags;i++)
					{
						if(strcmp(cstr, tags[i])==0)
						{
							type=TAG;
							break;
						}
					}
					if(type==NONE)
					{
						fprintf(output, SPARSERR"\tUnrecognised identifier '%s'\n", SPARSARG, cstr);
						fprintf(output, SPMKLINE);
						if(daemon)
							printf(DSPARSERR"unrecognised identifier\n", DSPARSARG);
						tree_free(s.chain);
						return(1);
					}
				}
				if(!node)
				{
					node=(sel_elt *)malloc(sizeof(sel_elt));
				}
				else
				{
					node->nextrel=nextrel;nextrel=SELF;
					node=node->next=(sel_elt *)malloc(sizeof(sel_elt));
				}
				node->type=type;
				node->data=cstr;
				cstr=NULL;cstl=0; // disconnect the pointer
				state=0; // return to reading-state
				igwhite=false;
			break;
			default:
				fprintf(output, SPARSERR"\tNo such state!\n", SPARSARG);
				fprintf(output, SPMKLINE);
				if(daemon)
					printf(DSPARSERR"no such state\n", DSPARSARG);
				tree_free(s.chain);
				return(1);
			break;
		}
	}
	return(0);
}

void tree_free(sel_elt * node)
{
	if(node)
	{
		tree_free(node->next);
		if(node->data)
			free(node->data);
		free(node);
	}
}
