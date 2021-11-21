c:\ntddk\bin\win_me\bin\ML /coff /c /Cx /Fl /Sn /DMASM6 /DBLD_COFF /DIS_32 /DWIN40COMPAT /I"c:\ntddk\inc\win_me" ide21201.asm

rem c:\98ddk\bin\win98\ML /coff /c /Cx /Fl /Sn /DMASM6 /DBLD_COFF /DIS_32 /DWIN40COMPAT /I"c:\98ddk\inc\win98" ide21201.asm

rem c:\95ddk\masm611c\ML /coff /c /Cx /Fl /Sn /DMASM6 /DBLD_COFF /DIS_32 /DWIN40COMPAT /I"c:\95ddk\inc32" ide21201.asm

c:\ntddk\bin\Link /VXD /DEF:ide21201.def ide21201.obj

rem c:\98ddk\bin\Link /VXD /DEF:ide21201.def ide21201.obj

rem c:\95ddk\msvc20\Link /VXD /DEF:ide21201.def ide21201.obj

