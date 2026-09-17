# Mirror of the Makefile's ConvertAIPRINT: turn PRINT("...") into
# AI_PRINT plus a NUL-terminated character list, exactly as the matching
# build feeds IDO.  Keep in sync with Makefile line ~74.
:loop
s/PRINT\("(..*?)(.)"/PRINT\("\1",'\2'/g
tloop
s/(PRINT\(.*?)'\\','(.)'(.*)\)/\1'\\\2'\3\)/g
s/(PRINT\()"(.)"(.*)\)/\1'\2'\3\)/g
s/PRINT\((.*)\)/PRINT\(\1,'\\0'\,)/g
s/PRINT\((.*)\)/AI_PRINT,\1/g
