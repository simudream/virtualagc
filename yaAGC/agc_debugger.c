/*
 * agc_debugger.c
 *
 *  Created on: Dec 5, 2008
 *      Author: MZ211D
 */

#include <stdio.h>
#include "agc_cli.h"
#include "agc_engine.h"
#include "agc_debugger.h"
#include "agc_gdbmi.h"

static Debugger_t Debugger;
static char FuncName[128];

// JMS: Variables pertaining to the symbol table loaded
int HaveSymbols = 0;    // 1 if we have a symbol table
char* SymbolFile;       // The name of the symbol table file

/* These globals will be deprecated when debugger is mature */
int DebugMode;
int RunState;

int InitializeDebugger(Options_t* Options,agc_t* State)
{
	Debugger.Options = Options;
	Debugger.RunState = 0;
	Debugger.State = State;

	/* Will remove these variables when debugger is mature */
	SingleStepCounter = 0;
	DebugMode = 1;
	RunState = Debugger.RunState;

	/* if the symbolfile is provided load the symbol table */
	if (Options->symtab)
	{
		/* Set the old global vars */
		SymbolFile = Options->symtab;
		HaveSymbols = 1;

		/* Reset and attempt to load the symbol table */
		ResetSymbolTable ();
		if (ReadSymbolTable(Options->symtab)) HaveSymbols = 0; /* Default is 0 */

		/* In the future Have Symbols will only be used by the Debugger */
		Debugger.HaveSymbols = HaveSymbols;
	}

	return 0;
}

int MonitorBreakpoints(void)
{
	int Break;
	int CurrentZ;
	int CurrentBB;
	int i;
	int Value;

	Value = GetFromZ(Debugger.State);
	CurrentZ = Debugger.State->Erasable[0][RegZ];
	CurrentBB = (Debugger.State->Erasable[0][RegBB] & 076007)|
	            (Debugger.State->InputChannel[7] & 0100);

	for (Break = i = 0; i < NumBreakpoints; i++)
		if (Breakpoints[i].WatchBreak == 2 &&
			gdbmiCheckBreakpoint(Debugger.State,&Breakpoints[i]))
		{
			// Pattern!
			if (Breakpoints[i].Address12 == (Value & Breakpoints[i].vRegBB))
			{
				printf ("Hit pattern, Value=" PAT " Mask=" PAT
				".\n", Breakpoints[i].Address12,
				Breakpoints[i].vRegBB);
				gdbmiUpdateBreakpoint(Debugger.State,&Breakpoints[i]);
				Break = 1;
				break;
			}
		}
		else if (Breakpoints[i].WatchBreak == 0 &&
			     gdbmiCheckBreakpoint(Debugger.State,&Breakpoints[i]))
		{
			int Address12, vRegBB;
			int CurrentFB, vCurrentFB;
			Address12 = Breakpoints[i].Address12;
			if (Address12 != CurrentZ) continue;

			if (Address12 < 01400)
			{
				printf ("Breakpoint %d, %s () at %s:%d\n",i+1,
				 gdbmiConstructFuncName(Breakpoints[i].Line,FuncName,127),
				 Breakpoints[i].Line->FileName,Breakpoints[i].Line->LineNumber);
				gdbmiUpdateBreakpoint(Debugger.State,&Breakpoints[i]);
				Break = 1;
				break;
			}
			if (Address12 >= 04000)
			{
				printf ("Breakpoint %d, %s () at %s:%d\n",i+1,
				 gdbmiConstructFuncName(Breakpoints[i].Line,FuncName,127),
				 Breakpoints[i].Line->FileName,
				 Breakpoints[i].Line->LineNumber);
				gdbmiUpdateBreakpoint(Debugger.State,&Breakpoints[i]);
				Break = 1;
				break;
			}

			vRegBB = Breakpoints[i].vRegBB;
			if (Address12 >= 01400 && Address12 < 02000 &&
			(vRegBB & 7) == (CurrentBB & 7))
			{
				// JMS: I'm not convinced yet that we can have a
				// breakpoint in erasable memory that has a symbol
				if (Breakpoints[i].Symbol != NULL)
				  printf ("Hit breakpoint %s at E%o,%05o.\n",Breakpoints[i].Symbol->Name, CurrentBB & 7, Address12);
				else
				  printf("Hit breakpoint at E%o,%05o.\n",CurrentBB & 7,Address12);

				gdbmiUpdateBreakpoint(Debugger.State,&Breakpoints[i]);
				Break = 1;
				break;
			}

			CurrentFB = (CurrentBB >> 10) & 037;
			if (CurrentFB >= 030 && (CurrentBB & 0100)) CurrentFB += 010;

			vCurrentFB = (vRegBB >> 10) & 037;
			if (vCurrentFB >= 030 && (vRegBB & 0100)) vCurrentFB += 010;

			if (Address12 >= 02000 && Address12 < 04000 &&
			CurrentFB == vCurrentFB)
			{
				int Bank;

				Bank = (CurrentBB >> 10) & 037;
				if (0 != (CurrentBB & 0100) && Bank >= 030) Bank += 010;

				if (Breakpoints[i].Symbol != NULL)
				  printf ("Hit breakpoint %s at %02o,%05o.\n",
					  Breakpoints[i].Symbol->Name, Bank, Address12);
				else
				  printf ("Hit breakpoint at %02o,%05o.\n",
					  Bank, Address12);
				  gdbmiUpdateBreakpoint(Debugger.State,&Breakpoints[i]);
				Break = 1;
				break;
			}
		}
		else if ((Breakpoints[i].WatchBreak == 1 &&
				  gdbmiCheckBreakpoint(Debugger.State,&Breakpoints[i]) &&
			      Breakpoints[i].WatchValue != GetWatch(Debugger.State,&Breakpoints[i])) ||
			     (Breakpoints[i].WatchBreak == 3 &&
			      Breakpoints[i].WatchValue == GetWatch (Debugger.State,&Breakpoints[i])))
		  {
			int Address12, vRegBB, Before, After;
			Address12 = Breakpoints[i].Address12;
			Before = (Breakpoints[i].WatchValue & 077777);
			After = (GetWatch (Debugger.State, &Breakpoints[i]) & 077777);
			if (Address12 < 01400)
			{
				if (Breakpoints[i].Symbol != NULL)
				  printf ("Hit watchpoint %s at %05o, %06o -> %06o.\n",
					  Breakpoints[i].Symbol->Name, Address12,Before, After);
				else
				  printf ("Hit watchpoint at %05o, %06o -> %06o.\n",
					  Address12, Before, After);

				if (Breakpoints[i].WatchBreak == 1)
				    Breakpoints[i].WatchValue = GetWatch (Debugger.State, &Breakpoints[i]);

				gdbmiUpdateBreakpoint(Debugger.State,&Breakpoints[i]);
				Break = 1;
				break;
			}

			vRegBB = Breakpoints[i].vRegBB;

			if (Address12 >= 01400 && Address12 < 02000 &&
			    (vRegBB & 7) == (CurrentBB & 7))
			{
				if (Breakpoints[i].Symbol == NULL)
				  printf
					("Hit watchpoint at E%o,%05o, %06o -> %06o.\n",
					 CurrentBB & 7, Address12, Before, After);
				else
				  printf
					("Hit watchpoint %s at E%o,%05o, %06o -> %06o.\n",
					 Breakpoints[i].Symbol->Name, CurrentBB & 7,
					 Address12, Before, After);

				if (Breakpoints[i].WatchBreak == 1)
				  Breakpoints[i].WatchValue = GetWatch (Debugger.State, &Breakpoints[i]);

				gdbmiUpdateBreakpoint(Debugger.State,&Breakpoints[i]);
				Break = 1;
				break;
			}
		 }
		else if ((Breakpoints[i].WatchBreak == 4 &&
				  gdbmiCheckBreakpoint(Debugger.State,&Breakpoints[i]) &&
				  Breakpoints[i].WatchValue != GetWatch (Debugger.State,&Breakpoints[i])))
		{
			int Address12, vRegBB, Before, After;

			Address12 = Breakpoints[i].Address12;
			Before = (Breakpoints[i].WatchValue & 077777);
			After = (GetWatch (Debugger.State, &Breakpoints[i]) & 077777);

			if (Address12 < 01400)
			{
				if (Breakpoints[i].Symbol != NULL)
				  printf ("%s=%06o\n", Breakpoints[i].Symbol->Name, After);
				else
				  printf ("(%05o)=%06o\n", Address12, After);
				Breakpoints[i].WatchValue = GetWatch (Debugger.State, &Breakpoints[i]);
			}
			else
			{
				vRegBB = Breakpoints[i].vRegBB;
				if (Address12 >= 01400 && Address12 < 02000 &&
					(vRegBB & 7) == (CurrentBB & 7))
				{
					if (Breakpoints[i].Symbol == NULL)
						printf ("(E%o,%05o)=%06o\n", CurrentBB & 7, Address12, After);
					else
						printf ("%s=%06o\n", Breakpoints[i].Symbol->Name, After);

					Breakpoints[i].WatchValue = GetWatch (Debugger.State, &Breakpoints[i]);
				}
			}
		}

	return (Break);
}

void DisplayInnerStackFrame(void)
{
    // If we have the symbol table, then print out the actual source,
    // rather than just a disassembly
    if (Debugger.Options->symtab)
	{
		// Resolve the current program counter into an entry into
		// the program line table. We pass in the current value of
		// the Z register, but also need the BB register and the
		// super-bank bit to resolve addresses.
		int CurrentZ = Debugger.State->Erasable[0][RegZ] & 07777;
		int FB = 037 & (Debugger.State->Erasable[0][RegBB] >> 10);
		int SBB = (Debugger.State->OutputChannel7 & 0100) ? 1 : 0;

		/* Get the SymbolLine for the InnerFrame */
		SymbolLine_t *Line = ResolveLineAGC(CurrentZ, FB, SBB);

		// There are several ways this can fail, and if either does we
		// just want to disasemble: if we didn't find the line in the
		// table or if ListSource() fails.
		if (Line)
		{
			/* Load the actual Source Line */
			LoadSourceLine(Line->FileName, Line->LineNumber);
			if (RunState)
			{
			  if (Debugger.Options->fullname) gdbmiPrintFullNameContents(Line);
			  else Disassemble (Debugger.State);
			}
		}
	}
    else Disassemble (Debugger.State);
}