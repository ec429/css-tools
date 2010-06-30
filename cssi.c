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

#define SPARSERR	"cssi: Error (Sel-Parser, state %d) row %d, col %d\n"
#define SPARSARG	state, sid, pos+1

#define SPARSEWARN	"cssi: warning: (Sel-Parser, state %d) row %d, col %d\n"
#define SPARSEWARG	state, sid, pos+1

#define DSPARSERR	"ERR:ESPARSE:%d,%d.%d:"
#define DSPARSARG	state, sid, pos /* note, this is 0-based */

#define DSPARSEWARN	"WARN:WSPARSE:%d,%d.%d:"
#define DSPARSEWARG	state, sid, pos /* note, this is 0-based */

#define SPMKLINE	"%.*s/* <- */%s\n", pos+1, s->text, s->text+pos+1

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
	SBLG,	// Adjacent Sibling ("E + F")
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

typedef struct _sel_elt3 // selfing
{
	seltype type;
	char * data; // eg "#foo" becomes type=ID, data="foo"; "[bar]" becomes type=ATTR, data="bar".
	struct _sel_elt3 * next; // linked-list
}
sel_elt3;

typedef struct _sel_elt2 // sibling
{
	sel_elt3 * selfs; // Should never be NULL
	family nextrel; // should be SBLG (or Generalised sibling, when we implement that (~))
	struct _sel_elt2 * next; // linked-list
	struct _sel_elt2 * prev; // doubly!
}
sel_elt2;

typedef struct _sel_elt // childing/descing
{
	sel_elt2 * sibs; // NULL means "*"
	family nextrel; // should be CHLD or DESC
	struct _sel_elt * next; // linked-list
	struct _sel_elt * prev; // doubly!
}
sel_elt;

typedef struct
{
	char * text; // when we parse this
	sel_elt * chain; // it goes here
	int ent; // index into entries table
	int dup; // 0=no duplicates, NZ num=first sel of dup block
	bool lmatch; // was it matched by the last test() run?
}
selector;

// function protos
char * fgetl(FILE *); // gets a line of string data; returns a malloc-like pointer (preserves trailing \n)
char * getl(char *); // like fgetl(stdin) but prints a prompt too (strips trailing \n)
selector * selmergesort(selector * array, int len);
int parse_selector(selector *, int);
void tree_free(sel_elt * node);
int treecmp(sel_elt * left, sel_elt * right);
bool test(int parmc, char *parmv[], selector * sort, int i, entry * entries, char ** filename, int nrows, int nsels, bool *err);
bool tree_match(sel_elt * curr, sel_elt * match);
bool tree_match_real(sel_elt *curr, sel_elt *match);

// global vars
FILE *output;
bool daemonmode=false; // are we talking to another process? -d to set

int main(int argc, char *argv[])
{
	/*unsigned char version_maj, version_min, version_rev;
	sscanf(VERSION, "%hhu.%hhu.%hhu", &version_maj, &version_min, &version_rev);*/
	output=stdout;
	int nfiles=0;
	char ** filename=NULL; // files to load
	char *importpath="";
	char ** assoc_ipath=NULL;
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
			daemonmode=true;
			output=stderr;
			fprintf(output, "cssi: Daemon mode is active.\n");
			printf("CSSI:\"%s\"\n", VERSION);//%hhu.%hhu.%hhu\n", version_maj, version_min, version_rev);
		}
		else if((strcmp(argt, "-t")==0)||(strcmp(argt, "--trace")==0))
		{
			trace=true;
			fprintf(output, "cssi: Tracing on stderr\n");
		}
		else if(strcmp(argt, "-Wall")==0) // TODO:generically handle warnings, so I don't have to remember to add each new warning to -Wall and -Wno-all
		{
			wnewline=true;
			wdupfile=true;
			watrule=true;
		}
		else if(strcmp(argt, "-Wno-all")==0)
		{
			wnewline=false;
			wdupfile=false;
			watrule=false;
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
			if(importpath[strlen(importpath)-1]!='/')
			{
				char *p=(char *)malloc(strlen(importpath)+2);
				sprintf(p, "%s/", importpath);
				importpath=p; // technically this leads to a memory leak, since importpath never gets free()d - but since you're not likely to give more than a few -I= options, it shouldn't matter
			}
		}
		else if((strncmp(argt, "-w=", 3)==0)||(strncmp(argt, "--max-warn=", 11)==0))
		{
			sscanf(strchr(argt, '=')+1, "%d", &maxwarnings);
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
		fprintf(output, "cssi: Error: No file given on command line!\n"USAGE_STRING"\n");
		if(daemonmode)
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
					if(daemonmode)
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
			if(daemonmode)
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
				if(daemonmode)
					printf("ERR:EMEM\n");
				return(1);
			}
			mfile[nlines-1]=fgetl(fp);
			if(!mfile[nlines-1])
			{
				fprintf(output, "cssi: Error: Failed to alloc mem for input file.\n");
				perror("malloc/realloc");
				if(daemonmode)
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
		if(daemonmode)
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
					if(daemonmode)
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
							if(daemonmode)
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
								if(daemonmode)
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
									if(daemonmode)
										printf(DPARSERR"malformed @import directive\n", DPARSARG);
									return(2);
								}
								url++;
								char *endurl=strchr(url, ')');
								if(!endurl)
								{
									fprintf(output, PARSERR"\tMalformed @import directive\n", PARSARG);
									fprintf(output, PMKLINE);
									if(daemonmode)
										printf(DPARSERR"malformed @import directive\n", DPARSARG);
									return(2);
								}
								*endurl=0;
								pos=(endurl-mfile[line])+1;
								nfiles++;
								filename=(char **)realloc(filename, nfiles*sizeof(char *));
								filename[nfiles-1]=(char *)malloc(strlen(assoc_ipath[i])+strlen(url)+1);
								sprintf(filename[nfiles-1], "%s%s", assoc_ipath[i], url);
								assoc_ipath=(char **)realloc(assoc_ipath, nfiles*sizeof(char *));
								assoc_ipath[nfiles-1]=assoc_ipath[i];
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
								if(daemonmode)
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
									if(daemonmode)
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
									if(daemonmode)
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
						if(daemonmode)
							printf(DPARSERR"no such state\n", DPARSARG);
						return(2);
					break;
				}
			}
		}
		free(mfile);
		fprintf(output, "cssi: parsed %s\n", i==nfiles?"<stdin>":filename[i]);
		if(daemonmode)
			printf("PARSED:\"%s\"\n", i==nfiles?"<stdin>":filename[i]); // Warning; it is possible to have a file named '<stdin>', though unlikely
		skip:;
	}
	fprintf(output, "cssi: Parsing completed\n");
	if(daemonmode)
		printf("PARSED*\n");
	if(nwarnings>maxwarnings)
	{
		fprintf(output, "cssi: warning: %d more warnings were not displayed.\n", nwarnings-maxwarnings);
		if(daemonmode)
			printf("XSWARN:%d\n", nwarnings-maxwarnings);
	}
	
	fprintf(output, "cssi: collating & parsing selectors\n");
	if(daemonmode)
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
			sels[nsels-1].dup=0;
		}
	}
	
	int nerrs=0;
	for(i=0;i<nsels;i++)
	{
		int e;
		if((e=parse_selector(&sels[i], i))) // assigns & tests NZ
		{
			nerrs++;
		}
	}
	
	selector * sort=selmergesort(sels, nsels);
	int dup=0;
	for(i=0;i<nsels-1;i++)
	{
		if(treecmp(sort[i].chain, sort[i+1].chain)==0)
		{
			sort[i].dup=dup?dup:(dup=i);
			sort[i+1].dup=dup;
		}
		else
			dup=0;
	}
	
	fprintf(output, "cssi: collated & parsed selectors\n");
	if(nerrs)
		fprintf(output, "cssi:  there were %d errors.\n", nerrs);
	if(daemonmode)
		printf("COLL*:%d\n", nerrs);
	
	int errupt=0;
	while(!errupt)
	{
		char * input=getl("cssi>");
		char * cmd=strtok(input, " ");
		int parmc=0; // the names are, of course, modelled on argc and argv.  TODO: pipelines (will require considerable encapsulation)
		char ** parmv=NULL;
		char *p;
		bool err=false;
		while((p=strtok(NULL, " ")))
		{
			parmc++;
			parmv=(char **)realloc(parmv, parmc*sizeof(char *));
			parmv[parmc-1]=p;
		}
		if(cmd)
		{
			if(strncmp(cmd, "selector", strlen(cmd))==0) // selectors
			{
				if(daemonmode)
					printf("SEL...\n"); // line ending with '...' indicates "continue until a line is '.'"
				else
					fprintf(output, "cssi: listing SELECTORS\n");
				int nrows=0;
				for(i=0;i<nsels;i++)
				{
					bool show=test(parmc, parmv, sort, i, entries, filename, nrows, nsels, &err);
					if(err) break;
					int ent=sort[i].ent;
					int file=entries[ent].file;
					if(show)
					{
						nrows++;
						if(daemonmode)
							printf("RECORD:ID=%d:FILE=\"%s\":LINE=%d:DUP=%d:SEL=\"%s\"\n", i, file<nfiles?filename[file]:"<stdin>", entries[ent].line+1, sort[i].dup, sort[i].text);
						else
							fprintf(output, "%d%s\tIn %s at %d:\t%s\n", i, sort[i].dup?sort[i].dup==i?"*":"+":"", file<nfiles?filename[file]:"<stdin>", entries[ent].line+1, sort[i].text);
					}
				}
				if(daemonmode)
					printf(".\n");
			}
			else if(strncmp(cmd, "declaration", strlen(cmd))==0) // contents of a sel's {}
			{
				if(daemonmode)
					printf("DECL...\n"); // line ending with '...' indicates "continue until a line is '.'"
				else
					fprintf(output, "cssi: listing DECLARATIONS\n");
				int nrows=0;
				for(i=0;i<nsels;i++)
				{
					bool show=test(parmc, parmv, sort, i, entries, filename, nrows, nsels, &err);
					if(err) break;
					int ent=sort[i].ent;
					if(show)
					{
						nrows++;
						if(daemonmode)
							printf("RECORD:ID=%d:DECL=\"%s\"\n", i, entries[ent].innercode);
						else
							fprintf(output, "%d\t{%s}\n", i, entries[ent].innercode);
					}
				}
				if(daemonmode)
					printf(".\n");
			}
			else if(strncmp(cmd, "quit", strlen(cmd))==0) // quit
			{
				// no params yet
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

// Sorts by sel_elt * chain, so you MUST parse_selector() first! (else they'll all be NULL so they'll all compare equal)
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
			if(treecmp(array[0].chain, array[1].chain)<=0)
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
			selector *sleft=selmergesort(left, i);
			selector *sright=selmergesort(right, len-i);
			free(left);
			free(right);
			int p=0,q=0;
			for(j=0;j<len;j++)
			{
				if(p==i)
				{
					rv[j]=sright[q++];
				}
				else if(q==len-i)
				{
					rv[j]=sleft[p++];
				}
				else
				{
					if(treecmp(sleft[p].chain, sright[q].chain)<=0)
					{
						rv[j]=sleft[p++];
					}
					else
					{
						rv[j]=sright[q++];
					}
				}
			}
			free(sleft);
			free(sright);
			return(rv);
		break;
	}
}

int parse_selector(selector * s, int sid)
{
	s->chain=NULL; // initially empty
	// state machine
	int state=0;
	int pos=0;
	char *curr;
	char *cstr=NULL;
	int cstl=0;
	bool desc=false;
	sel_elt *chld=NULL;
	sel_elt2 *sblg=NULL;
	sel_elt3 *self=NULL;
	seltype type=NONE;
	bool igwhite=false;
	while(*(curr=s->text+pos) || state) // assigns curr to the current position, then checks the char there is not '\0' - if it is, and state=0, then stop
	{
		switch(state)
		{
			case 0: // get an identifier
				if(*curr=='.')
				{
					if(desc && chld)
					{
						sel_elt * next=(sel_elt *)malloc(sizeof(sel_elt));
						next->prev=chld;
						chld->nextrel=DESC;
						chld=chld->next=next;
						chld->next=NULL;
						sblg=chld->sibs=NULL;
						self=NULL;
						desc=false;
					}
					pos++;
					state=1;
					type=CLASS;
					cstr=NULL;cstl=0;
				}
				else if(*curr==':')
				{
					if(desc && chld)
					{
						sel_elt * next=(sel_elt *)malloc(sizeof(sel_elt));
						next->prev=chld;
						chld->nextrel=DESC;
						chld=chld->next=next;
						chld->next=NULL;
						sblg=chld->sibs=NULL;
						self=NULL;
						desc=false;
					}
					pos++;
					state=1;
					type=PCLASS;
					cstr=NULL;cstl=0;
				}
				else if(*curr=='#')
				{
					if(desc && chld)
					{
						sel_elt * next=(sel_elt *)malloc(sizeof(sel_elt));
						next->prev=chld;
						chld->nextrel=DESC;
						chld=chld->next=next;
						chld->next=NULL;
						sblg=chld->sibs=NULL;
						self=NULL;
						desc=false;
					}
					pos++;
					state=1;
					type=ID;
					cstr=NULL;cstl=0;
				}
				else if(*curr=='*')
				{
					if(desc && chld)
					{
						sel_elt * next=(sel_elt *)malloc(sizeof(sel_elt));
						next->prev=chld;
						chld->nextrel=DESC;
						chld=chld->next=next;
						chld->next=NULL;
						sblg=chld->sibs=NULL;
						self=NULL;
						desc=false;
					}
					type=UNIV;
					cstr=NULL;
					state=2;
					pos++;
				}
				else if(strchr(" \t\n\r\f", *curr)) // whitespace (though \n should /not/ happen)
				{
					if(chld && !igwhite) // ignore ws at the beginning of a sel or after a > or + (or suchlike)
						desc=true;
					pos++;
				}
				else if(*curr=='>')
				{
					sel_elt * next=(sel_elt *)malloc(sizeof(sel_elt));
					if(!chld)
					{
						s->chain=chld=(sel_elt *)malloc(sizeof(sel_elt));
						chld->prev=NULL;
						chld->sibs=NULL;
					}
					next->prev=chld;
					chld->nextrel=CHLD;
					chld=chld->next=next;
					chld->next=NULL;
					sblg=chld->sibs=NULL;
					self=NULL;
					desc=false;
					pos++;
					igwhite=true; // ' > ' is like '>', not ' '.
				}
				else if(*curr=='+')
				{
					sel_elt2 * next=(sel_elt2 *)malloc(sizeof(sel_elt2));
					if(!sblg)
					{
						chld->sibs=sblg=(sel_elt2 *)malloc(sizeof(sel_elt2));
						sblg->prev=NULL;
						sblg->selfs=NULL;
					}
					next->prev=sblg;
					sblg->nextrel=SBLG;
					sblg=sblg->next=next;
					sblg->selfs=NULL;
					desc=false;
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
						if(daemonmode)
							printf(DSPARSERR"empty selent\n", DSPARSARG);
						tree_free(s->chain);
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
						if(daemonmode)
							printf(DSPARSERR"unrecognised identifier\n", DSPARSARG);
						tree_free(s->chain);
						return(1);
					}
				}
				if(desc&&chld)
				{
					sel_elt * next=(sel_elt *)malloc(sizeof(sel_elt));
					next->prev=chld;
					chld->nextrel=DESC;
					chld=chld->next=next;
					chld->next=NULL;
					sblg=chld->sibs=NULL;
					self=NULL;
				}
				if(!sblg)
				{
					sblg=chld->sibs=(sel_elt2 *)malloc(sizeof(sel_elt2));
					sblg->next=NULL;
					sblg->prev=NULL;
					self=sblg->selfs=NULL;
				}
				if(!self)
				{
					self=(sel_elt3 *)malloc(sizeof(sel_elt3));
					sblg->selfs=self;
				}
				else
				{
					sel_elt3 * next=(sel_elt3 *)malloc(sizeof(sel_elt3));
					self=self->next=next;
				}
				self->type=type;
				self->data=cstr;
				self->next=NULL; // until (and if) we get one
				cstr=NULL;cstl=0; // disconnect the pointer
				state=0; // return to reading-state
				igwhite=false;
				desc=false;
			break;
			default:
				fprintf(output, SPARSERR"\tNo such state!\n", SPARSARG);
				fprintf(output, SPMKLINE);
				if(daemonmode)
					printf(DSPARSERR"no such state\n", DSPARSARG);
				tree_free(s->chain);
				return(1);
			break;
		}
	}
	return(0);
}

void tree_free3(sel_elt3 * node)
{
	if(node)
	{
		if(node->data)
			free(node->data);
		tree_free3(node->next);
		free(node);
	}
}

void tree_free2(sel_elt2 * node)
{
	if(node)
	{
		tree_free2(node->next);
		tree_free3(node->selfs);
		free(node);
	}
}

void tree_free(sel_elt * node)
{
	if(node)
	{
		tree_free(node->next);
		tree_free2(node->sibs);
		free(node);
	}
}

int treecmp3(sel_elt3 * left, sel_elt3 * right)
{
	if(left && right)
	{
		int dtype=left->type - right->type;
		if(dtype)
			return(dtype);
		int ddata=strcmp(left->data, right->data);
		if(ddata)
			return(ddata);
		return(treecmp3(left->next, right->next));
	}
	if(left)
		return(1);
	if(right)
		return(-1);
	return(0);
}

int treecmp2(sel_elt2 * left, sel_elt2 * right)
{
	if(left && right)
	{
		int drel=left->nextrel - right->nextrel;
		if(drel)
			return(drel);
		int dselfs=treecmp3(left->selfs, right->selfs);
		if(dselfs)
			return(dselfs);
		return(treecmp2(left->next, right->next));
	}
	if(left)
		return(1);
	if(right)
		return(-1);
	return(0);
}

int treecmp(sel_elt * left, sel_elt * right)
{
	if(left && right)
	{
		int drel=left->nextrel - right->nextrel;
		if(drel)
			return(drel);
		int dsibs=treecmp2(left->sibs, right->sibs);
		if(dsibs)
			return(dsibs);
		return(treecmp(left->next, right->next));
	}
	if(left)
		return(1);
	if(right)
		return(-1);
	return(0);
}

bool test(int parmc, char *parmv[], selector * sort, int i, entry * entries, char ** filename, int nrows, int nsels, bool *err)
{
	bool show=true;*err=false;
	bool lmatch=sort[i].lmatch;
	sort[i].lmatch=false;
	int ent=sort[i].ent;
	int file=entries[ent].file;
	int parm;
	for(parm=0;(parm<parmc)&&show;parm++)
	{
		char *sparm=strdup(parmv[parm]);
		char *cmp=sparm;
		while(*cmp && !strchr("=<>:", *cmp))
			cmp++;
		char wcmp=*cmp;
		*cmp=0;
		cmp++;
		bool eq=false;
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
			num=true;nmatch=entries[ent].line+(daemonmode?0:1); // daemon mode uses 0-based linenos
		}
		else if(strcmp(sparm, "match")==0)
		{
			tree=true;
			smatch=(char *)sort[i].chain; // it's a sel_elt *, really, not a char *
		}
		else if(strcmp(sparm, "dup")==0)
		{
			num=true;
			nmatch=sort[i].dup;
		}
		else if(strcmp(sparm, "last")==0)
		{
			num=true;
			nmatch=lmatch;
		}
		else if(strcmp(sparm, "rows")==0)
		{
			num=true;
			nmatch=nrows;
			wcmp='<';
		}
		else
		{
			if(daemonmode)
				printf("ERR:EBADPARM:BADPARAM:%d:\"%s\"\n", parm, parmv[parm]);
			else
				fprintf(output, "cssi: Error: Bad matcher %s (unrecognised param)\n", parmv[parm]);
			free(sparm);*err=true;return(false);
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
					// TODO: parse the tree outside the loop, rather than once for each sel in the list
					selector tmatch;
					tmatch.text=cmp;
					int e=parse_selector(&tmatch, -1);
					if(e)
					{
						free(sparm);*err=true;return(false);
					}
					sel_elt * curr=(sel_elt *)smatch;
					sel_elt * match=tmatch.chain;
					show&=tree_match(curr, match);
					tree_free(tmatch.chain);
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
				if(*cmp=='=')
				{
					eq=true;cmp++;sscanf(cmp, "%d", &inval);
				}
				if(num)
				{
					show&=eq?(nmatch <= inval):(nmatch < inval);
				}
				else
				{
					if(daemonmode)
						printf("ERR:EBADPARM:NUMCOMP:%d:\"%s\"\n", parm, parmv[parm]);
					else
						fprintf(output, "cssi: Error: '<' is for numerics only (%s)\n", parmv[parm]);
					free(sparm);*err=true;return(false);
				}
			break;
			case '>':
				if(*cmp=='=')
				{
					eq=true;cmp++;sscanf(cmp, "%d", &inval);
				}
				if(num)
				{
					show&=eq?(nmatch >= inval):(nmatch > inval);
				}
				else
				{
					if(daemonmode)
						printf("ERR:EBADPARM:NUMCOMP:%d:\"%s\"\n", parm, parmv[parm]);
					else
						fprintf(output, "cssi: Error: '>' is for numerics only (%s)\n", parmv[parm]);
					free(sparm);*err=true;return(false);
				}
			break;
			case ':':
				if(num||tree)
				{
					if(daemonmode)
						printf("ERR:EBADPARM:STRCOMP:%d:\"%s\"\n", parm, parmv[parm]);
					else
						fprintf(output, "cssi: Error: ':' is for strings only (%s)\n", parmv[parm]);
					free(sparm);*err=true;return(false);
				}
				else
				{
					if(daemonmode)
						printf("ERR:ENOSYS:REGEXMATCH:%d:\"%s\"\n", parm, parmv[parm]);
					else
						fprintf(output, "cssi: Error: regex-matching unimplemented (%s)\n", parmv[parm]);
					free(sparm);*err=true;return(false);
				}
			break;
			case 0:
				if(num)
					show=(nmatch!=0);
				else if(tree)
				{
					if(daemonmode)
						printf("ERR:ENOSYS:TREEMATCH:%d:\"%s\"\n", parm, parmv[parm]);
					else
						fprintf(output, "cssi: Error: tree-matching unimplemented (%s)\n", parmv[parm]);
					free(sparm);*err=true;return(false);
				}
				else
					show=smatch?smatch[0]:0;
			break;
			default: // this should be impossible
				if(daemonmode)
					printf("ERR:EBADPARM:BADCOMP:%d:\"%s\"\n", parm, parmv[parm]);
				else
					fprintf(output, "cssi: Error: Bad matcher %s (bad comparator)\n", parmv[parm]);
				free(sparm);*err=true;return(false);
			break;
		}
		free(sparm);
	}
	sort[i].lmatch=show;
	return(show);
}

bool tree_match(sel_elt * curr, sel_elt * match)
{
	if(curr)
	{
		sel_elt * clast=curr;
		while(clast->next) clast=clast->next;
		sel_elt * mlast=match;
		while(mlast && mlast->next) mlast=mlast->next;
		return tree_match_real(clast, mlast); // a match of NULL means prepension is completely acceptable - "match=" matches everything
	}
	else return(true); // * matches everything
}

bool tree_match_real(sel_elt *curr, sel_elt *match)
{
	
}
