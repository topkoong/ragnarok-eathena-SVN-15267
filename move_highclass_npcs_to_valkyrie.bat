@echo off
REM ============================================================
REM Move High-Class / Rebirth NPCs to official-style Valkyrie map
REM This is binary-safe because it only edits ASCII parts.
REM It will not corrupt Thai CP874/TIS-620 dialogue text.
REM ============================================================

cd /d "%~dp0"

python -c "from pathlib import Path; import re; files=[('npc/custom/thai_rebirth_valkyrie.txt',rb'(prontera|valkyrie),[0-9]+,[0-9]+,[0-9]+\s+script\s+RebirthNPC#rebirth\s+[0-9]+\s*,\s*\{',rb'valkyrie,48,86,4\tscript\tRebirthNPC#rebirth\t811,{'),('npc/custom/thai_highclass_jobchanger.txt',rb'(prontera|valkyrie),[0-9]+,[0-9]+,[0-9]+\s+script\s+HighClassNPC#highclass\s+[0-9]+\s*,\s*\{',rb'valkyrie,48,39,5\tscript\tHighClassNPC#highclass\t742,{')]; [Path(p).write_bytes(re.sub(pattern,repl,Path(p).read_bytes(),count=1)) or print('UPDATED:',p) for p,pattern,repl in files]"

echo.
echo Done. Now run @reloadscript in-game.
pause