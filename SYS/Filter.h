
// The name of the System process, in which context we're called in our DriverEntry
#define SYSNAME "System"

// Maximum separate filter components 
#define MAXFILTERS 64

// This is the offset into a KPEB of the current process name. This is determined
// dynamically by scanning the process block belonging to the GUI for its name.
ULONG ProcessNameOffset;
// Global filter (sent to us by the GUI)
FILTER FilterDef;
// Lock to protect filter arrays
KMUTEX FilterMutex;
// Array of process filters 
ULONG NumProcessIncludeFilter = 0;
ULONG NumProcessExcludeFilter = 0;
PCHAR ProcessIncludeFilter[MAXFILTERS];
PCHAR ProcessExcludeFilter[MAXFILTERS];

// Boyer-MooreËã·¨µÄ×Ö·û´®Æ¥Åä
PUCHAR BmSubString( PUCHAR strText, PUCHAR strPattern, ULONG lenText, ULONG lenPattern )
{
	ULONG i,k;
	LONG j;
	ULONG pos[256] = {0};
	for (i=0; i<lenPattern-1; i++)
	{
		pos[strPattern[i]] = i+1;
	}

	i = lenPattern-1;
	while (i<lenText)
	{
		j = lenPattern-1;
		k = i;
		while (j >= 0 && strPattern[j] == strText[k])
		{
			--j;
			--k;
		}
		if (j<0)
		{
			return strText+i-lenPattern+1;
		}
		else
		{
			i += lenPattern-pos[strText[i]];
		}
	}
	return NULL;
}

// Only thing left after compare is more mask. This routine makes
// sure that its a valid wild card ending so that its really a match.
#define MatchOkay(Pattern)	( (*Pattern && *Pattern != '*') ? FALSE : TRUE )

// Performs nifty wildcard comparison.
BOOLEAN MatchWithPattern( PCHAR Pattern, PCHAR Name )
{
	CHAR upcase;
	// End of pattern?
	if (!*Pattern)
	{
		return FALSE;
	}
	// If we hit a wildcard, do recursion
	if (*Pattern == '*')
	{
		Pattern++;
		while (*Name && *Pattern)
		{
			if (*Name >= 'a' && *Name <= 'z')
			{
				upcase = *Name - 'a' + 'A';
			}
			else
			{
				upcase = *Name;
			}

			// See if this substring matches
			if (*Pattern == upcase || *Name == '*')
			{
				if (MatchWithPattern( Pattern+1, Name+1 ))
					return TRUE;
			}
			// Try the next substring
			Name++;
		}

		// See if match condition was met
		return MatchOkay( Pattern );
	} 
	// Do straight compare until we hit a wildcard
	while (*Name && *Pattern != '*')
	{
		if (*Name >= 'a' && *Name <= 'z')
			upcase = *Name - 'a' + 'A';
		else
			upcase = *Name;
		
		if (*Pattern == upcase)
		{
			Pattern++;
			Name++;
		}
		else
		{
			return FALSE;
		}
	}
	// If not done, recurse
	if (*Name)
	{
		return MatchWithPattern( Pattern, Name );
	}
	// If pattern isn't empty, it must be a wildcard
	return MatchOkay( Pattern );
}

// In an effort to remain version-independent, rather than using a 
// hard-coded into the KPEB (Kernel Process Environment Block), we
// scan the KPEB looking for the name, which should match that of the GUI process
ULONG GetProcessNameOffset(void)
{
	PEPROCESS curProc = PsGetCurrentProcess();
	// Scan for 12KB, hoping the KPEB never grows that big!
	PUCHAR curPtr = BmSubString( (PUCHAR)curProc, (PUCHAR)SYSNAME, 3*PAGE_SIZE, strlen(SYSNAME) );
	if (curPtr)
	{
		return curPtr-(PUCHAR)curProc;
	}
	else
	{
		return 0;
	}
}

// Uses undocumented data structure offsets to obtain the name of the currently executing process.
PCHAR GetProcessName( PCHAR Name )
{
	PEPROCESS curProc;
	PCHAR namePtr;
	ULONG i;
	
	// We only try and get the name if we located the name offset
	if (ProcessNameOffset)
	{
		curProc = PsGetCurrentProcess();
		namePtr = (PCHAR)curProc + ProcessNameOffset;
		strncpy( Name, namePtr, 16 );
		Name[16] = 0;
	}
	else
	{
		strcpy( Name, "???" );
	}
	KdPrint(("GetProcessName= %s\n", Name));

	// Apply process name filters
	MUTEX_WAIT( FilterMutex );
	for (i=0; i<NumProcessExcludeFilter; i++)
	{
		if (MatchWithPattern( ProcessExcludeFilter[i], Name ))
		{
			MUTEX_RELEASE( FilterMutex );
			return NULL;
		}
	}
	for (i=0; i<NumProcessIncludeFilter; i++)
	{
		if (MatchWithPattern( ProcessIncludeFilter[i], Name ))
		{
			MUTEX_RELEASE( FilterMutex );
			return Name;
		}
	}
	MUTEX_RELEASE( FilterMutex );
	return NULL;// Ä¬ÈÏ·µ»ØÖµ£¨Î´ÕÒµ½£©
}

// Takes a filter string and splits into components (a component is separated with a ';')
VOID MakeFilterArray( PCHAR FilterString,
						PCHAR FilterArray[],
						PULONG NumFilters )
{
	PCHAR filterStart;
	ULONG filterLength;

	filterStart = FilterString;// Scan through the process filters
	while (*filterStart)
	{
		filterLength = 0;
		while (filterStart[filterLength] && filterStart[filterLength] != ';')
		{
			filterLength++;
		}

		if (filterLength)// Ignore zero-length components
		{
			FilterArray[*NumFilters] = ExAllocatePool( PagedPool, filterLength+1 );
			strncpy( FilterArray[*NumFilters], filterStart, filterLength );
			FilterArray[*NumFilters][filterLength] = 0;
			(*NumFilters)++;
		}

		if (!filterStart[filterLength])
		{
			// Are we done?
			break;
		}
		else
		{
			// Move to the next component (skip over ';')
			filterStart += filterLength+1;
		}
	}
}

// Takes a new filter specification and updates the filter arrays with them.
VOID UpdateFilters(void)
{
	ULONG i;
	// Free old filters (if any)
	MUTEX_WAIT( FilterMutex );
	for (i=0; i<NumProcessIncludeFilter; i++)
	{
		ExFreePool( ProcessIncludeFilter[i] );
	}
	for (i=0; i<NumProcessExcludeFilter; i++)
	{
		ExFreePool( ProcessExcludeFilter[i] );
	}
	NumProcessIncludeFilter = 0;
	NumProcessExcludeFilter = 0;
	// Create new filter arrays
	MakeFilterArray( FilterDef.IncludeFilter, ProcessIncludeFilter, &NumProcessIncludeFilter );
	MakeFilterArray( FilterDef.ExcludeFilter, ProcessExcludeFilter, &NumProcessExcludeFilter );
	MUTEX_RELEASE( FilterMutex );
	KdPrint(("NumProcessIncludeFilter= %d, NumProcessExcludeFilter= %d\n", NumProcessIncludeFilter, NumProcessExcludeFilter));
}

