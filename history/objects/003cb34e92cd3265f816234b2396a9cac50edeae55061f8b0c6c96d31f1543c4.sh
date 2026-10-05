printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:0\n'
(
cd /TMP/CLIQA
[ 3 -gt 2 ]
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:0:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:1\n'
(
cd /TMP/CLIQA
[ 3 -lt 2 ]
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:1:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:2\n'
(
cd /TMP/CLIQA
echo -e 'a\tb\nc'
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:2:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:3\n'
(
cd /TMP/CLIQA
echo -n a b
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:3:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:4\n'
(
cd /TMP/CLIQA
printf 'a b
c
' | wc
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:4:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:5\n'
(
cd /TMP/CLIQA
printf 'a

b
' | nl
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:5:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:6\n'
(
cd /TMP/CLIQA
printf 'a	b
' | expand -t 4
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:6:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:7\n'
(
cd /TMP/CLIQA
printf '    a    b
' | unexpand -a -t 4
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:7:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:8\n'
(
cd /TMP/CLIQA
printf 'a\r\nb\r\n' | dos2unix
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:8:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:9\n'
(
cd /TMP/CLIQA
printf 'a\nb\n' | unix2dos
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:9:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:10\n'
(
cd /TMP/CLIQA
printf 'cat
dog
bird
' | egrep 'cat|dog'
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:10:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:11\n'
(
cd /TMP/CLIQA
printf 'a.b
axb
' | fgrep a.b
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:11:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:12\n'
(
cd /TMP/CLIQA
printf 'a
' | grep z
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:12:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:13\n'
(
cd /TMP/CLIQA
cmp A.TXT A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:13:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:14\n'
(
cd /TMP/CLIQA
cmp -s A.TXT B.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:14:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:15\n'
(
cd /TMP/CLIQA
comm A.TXT C.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:15:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:16\n'
(
cd /TMP/CLIQA
comm -12 A.TXT C.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:16:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:17\n'
(
cd /TMP/CLIQA
diff -s A.TXT A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:17:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:18\n'
(
cd /TMP/CLIQA
diff A.TXT B.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:18:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:19\n'
(
cd /TMP/CLIQA
cp A.TXT PATCHED; patch --dry-run PATCHED CHANGE.PATCH; cat PATCHED
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:19:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:20\n'
(
cd /TMP/CLIQA
cp A.TXT PATCHED; patch PATCHED CHANGE.PATCH; cat PATCHED
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:20:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:21\n'
(
cd /TMP/CLIQA
cp B.TXT PATCHED; patch -R PATCHED CHANGE.PATCH; cat PATCHED
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:21:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:22\n'
(
cd /TMP/CLIQA
printf 'A\x00\xff' | tee TEE.BIN; cat TEE.BIN
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:22:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:23\n'
(
cd /TMP/CLIQA
printf '\x00alpha\x00beta\n' | strings -n 4
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:23:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:24\n'
(
cd /TMP/CLIQA
length 'a b'
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:24:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:25\n'
(
cd /TMP/CLIQA
printf '\x00\x7f\xff' | od -An -tx1
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:25:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:26\n'
(
cd /TMP/CLIQA
printf 'A\x01\x7f\n' | catv
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:26:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:27\n'
(
cd /TMP/CLIQA
printf abcdefg | split -b 3 - SPLIT; cat SPLITaa SPLITab SPLITac
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:27:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:28\n'
(
cd /TMP/CLIQA
less A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:28:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:29\n'
(
cd /TMP/CLIQA
more A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:29:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:30\n'
(
cd /TMP/CLIQA
yes okay | head -n 3
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:30:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:31\n'
(
cd /TMP/CLIQA
getopt -o ab: -l long: -- -a -b 'a b' --long=x tail
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:31:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:32\n'
(
cd /TMP/CLIQA
getopt -q -o a -- -z
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:32:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:33\n'
(
cd /TMP/CLIQA
env M9QA='a b' printenv M9QA; printenv M9QA
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:33:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:34\n'
(
cd /TMP/CLIQA
printenv M9_MISSING_8392
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:34:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:35\n'
(
cd /TMP/CLIQA
mkdir -p ENV; printf ' value  
ignored' > ENV/M9QA; envdir ENV printenv M9QA
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:35:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:36\n'
(
cd /TMP/CLIQA
pwd
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:36:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:37\n'
(
cd /TMP/CLIQA
which cat
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:37:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:38\n'
(
cd /TMP/CLIQA
which missing_9381
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:38:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:39\n'
(
cd /TMP/CLIQA
realpath ./A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:39:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:40\n'
(
cd /TMP/CLIQA
realpath missing_9381
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:40:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:41\n'
(
cd /TMP/CLIQA
readlink -m ./sub/../missing
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:41:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:42\n'
(
cd /TMP/CLIQA
mkdir -p LIST; touch LIST/z LIST/a LIST/.hidden; ls -1 LIST
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:42:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:43\n'
(
cd /TMP/CLIQA
ls -A LIST
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:43:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:44\n'
(
cd /TMP/CLIQA
mkdir -p MK/A/B; test -d MK/A/B
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:44:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:45\n'
(
cd /TMP/CLIQA
mkdir MK
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:45:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:46\n'
(
cd /TMP/CLIQA
cp A.TXT TOUCH; touch TOUCH EMPTY; cat TOUCH; test -f EMPTY
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:46:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:47\n'
(
cd /TMP/CLIQA
cp BINARY.BIN COPY.BIN; cat COPY.BIN
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:47:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:48\n'
(
cd /TMP/CLIQA
mkdir -p CP/A; cp A.TXT CP/A/F; cp -r CP CP2; cat CP2/A/F
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:48:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:49\n'
(
cd /TMP/CLIQA
cp A.TXT A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:49:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:50\n'
(
cd /TMP/CLIQA
cp A.TXT MOVE; mv MOVE MOVED; cat MOVED; test ! -e MOVE
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:50:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:51\n'
(
cd /TMP/CLIQA
mkdir -p RM/A; cp A.TXT RM/A/F; rm -r RM; test ! -e RM
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:51:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:52\n'
(
cd /TMP/CLIQA
rm -f missing_9381
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:52:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:53\n'
(
cd /TMP/CLIQA
mkdir EMPTYDIR; rmdir EMPTYDIR; test ! -e EMPTYDIR
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:53:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:54\n'
(
cd /TMP/CLIQA
rmdir MK
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:54:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:55\n'
(
cd /TMP/CLIQA
find CP2 -type f -name F
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:55:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:56\n'
(
cd /TMP/CLIQA
du -s CP2
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:56:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:57\n'
(
cd /TMP/CLIQA
cp A.TXT META; chmod 310 META; stat META
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:57:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:58\n'
(
cd /TMP/CLIQA
chown 1200:1201 META; stat META
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:58:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:59\n'
(
cd /TMP/CLIQA
chgrp 1202 META; stat META
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:59:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:60\n'
(
cd /TMP/CLIQA
install -m 300 A.TXT INSTALLED; cat INSTALLED; stat INSTALLED
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:60:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:61\n'
(
cd /TMP/CLIQA
stat A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:61:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:62\n'
(
cd /TMP/CLIQA
dd if=A.TXT bs=2 skip=1 count=1
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:62:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:63\n'
(
cd /TMP/CLIQA
dd if=BINARY.BIN of=DD.BIN bs=3; cat DD.BIN
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:63:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:64\n'
(
cd /TMP/CLIQA
dd bs=0
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:64:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:65\n'
(
cd /TMP/CLIQA
printf abc | cksum
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:65:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:66\n'
(
cd /TMP/CLIQA
printf abc | sum -s
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:66:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:67\n'
(
cd /TMP/CLIQA
printf abc | sum -r
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:67:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:68\n'
(
cd /TMP/CLIQA
cat BINARY.BIN | pipe_progress
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:68:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:69\n'
(
cd /TMP/CLIQA
printf abc | gzip -c | zcat
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:69:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:70\n'
(
cd /TMP/CLIQA
printf bad | gunzip -c
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:70:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:71\n'
(
cd /TMP/CLIQA
printf bad | bunzip2 -c
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:71:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:72\n'
(
cd /TMP/CLIQA
unlzma -c /SYS/TEST/DATA.LZMA
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:72:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:73\n'
(
cd /TMP/CLIQA
tar cf QA.TAR A.TXT BINARY.BIN; tar tf QA.TAR
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:73:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:74\n'
(
cd /TMP/CLIQA
mkdir -p TAROUT; cd TAROUT; tar xf ../QA.TAR; cat TMP/CLIQA/BINARY.BIN
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:74:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:75\n'
(
cd /TMP/CLIQA
mkdir -p PARTS; cp /BIN/TRUE.SCX PARTS/a; cp /BIN/FALSE.SCX PARTS/z.skip; run-parts --test PARTS
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:75:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:76\n'
(
cd /TMP/CLIQA
run-parts PARTS
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:76:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:77\n'
(
cd /TMP/CLIQA
watch -t --count 2 -n 1 echo tick
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:77:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:78\n'
(
cd /TMP/CLIQA
watch -t --count 1 false
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:78:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:79\n'
(
cd /TMP/CLIQA
time printf done
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:79:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:80\n'
(
cd /TMP/CLIQA
time false
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:80:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:81\n'
(
cd /TMP/CLIQA
timeout 1 printf done
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:81:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:82\n'
(
cd /TMP/CLIQA
timeout 1 sleep 10
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:82:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:83\n'
(
cd /TMP/CLIQA
sleep 0
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:83:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:84\n'
(
cd /TMP/CLIQA
usleep 1
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:84:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:85\n'
(
cd /TMP/CLIQA
uptime
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:85:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:86\n'
(
cd /TMP/CLIQA
free
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:86:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:87\n'
(
cd /TMP/CLIQA
df
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:87:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:88\n'
(
cd /TMP/CLIQA
mountpoint /
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:88:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:89\n'
(
cd /TMP/CLIQA
mountpoint -q .
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:89:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:90\n'
(
cd /TMP/CLIQA
ps
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:90:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:91\n'
(
cd /TMP/CLIQA
top -b -n 1
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:91:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:92\n'
(
cd /TMP/CLIQA
uname -a
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:92:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:93\n'
(
cd /TMP/CLIQA
id
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:93:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:94\n'
(
cd /TMP/CLIQA
whoami
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:94:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:95\n'
(
cd /TMP/CLIQA
logname
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:95:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:96\n'
(
cd /TMP/CLIQA
tty
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:96:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:97\n'
(
cd /TMP/CLIQA
date +M9:%Y:%m:%d:%Z:%z
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:97:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:98\n'
(
cd /TMP/CLIQA
date -s 2020-01-01
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:98:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:99\n'
(
cd /TMP/CLIQA
cal 2 2024
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:99:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:100\n'
(
cd /TMP/CLIQA
hwclock
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:100:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:101\n'
(
cd /TMP/CLIQA
sleep 30 &
p=$!
sleep 1
kill "$p"; wait "$p"; test "$?" -ne 0
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:101:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:102\n'
(
cd /TMP/CLIQA
sleep 30 &
p=$!
sleep 1
pidof sleep.scx > PIDS; kill "$p"; wait "$p"; test -s PIDS
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:102:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:103\n'
(
cd /TMP/CLIQA
sleep 30 &
p=$!
sleep 1
pgrep -x -c sleep.scx; kill "$p"; wait "$p"; true
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:103:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:104\n'
(
cd /TMP/CLIQA
sleep 30 &
p=$!
sleep 1
pkill -x sleep.scx; r=$?; wait "$p"; test "$r" -eq 0
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:104:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:105\n'
(
cd /TMP/CLIQA
sleep 30 &
p=$!
sleep 1
killall sleep.scx; r=$?; wait "$p"; test "$r" -eq 0
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:105:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:106\n'
(
cd /TMP/CLIQA
logger -t qa -p 13 'm9 text'; logread
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:106:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:107\n'
(
cd /TMP/CLIQA
logger -t qa 'm9 marker'; logread | tail -n 1 | sed 's/^.*qa: //'
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:107:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:108\n'
(
cd /TMP/CLIQA
printf '* * * * * echo QACRON
' > CRON; crontab CRON; crontab -l
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:108:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:109\n'
(
cd /TMP/CLIQA
printf '99 * * * * echo bad
' > CRONBAD; crontab CRONBAD; test "$?" -ne 0; crontab -l
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:109:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:110\n'
(
cd /TMP/CLIQA
crontab -r; crontab -l
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:110:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:111\n'
(
cd /TMP/CLIQA
script -c 'printf script-ok' SESSION; cat SESSION
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:111:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:112\n'
(
cd /TMP/CLIQA
script -c 'printf failed; false' FAILED; r=$?; cat FAILED; test "$r" -eq 1
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:112:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:113\n'
(
cd /TMP/CLIQA
printf '0.01 3
0.01 3
' > TIMING; printf abcdef > REPLAY; scriptreplay TIMING REPLAY 100
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:113:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:114\n'
(
cd /TMP/CLIQA
printf '0.01 3
' > TIMING; scriptreplay TIMING REPLAY 100
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:114:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:115\n'
(
cd /TMP/CLIQA
printf 'a
hello
world
.
w EDITED
Q
' | ed; cat EDITED
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:115:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:116\n'
(
cd /TMP/CLIQA
adduser m9qa 1203 1203 /HOME/M9QA
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:116:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:117\n'
(
cd /TMP/CLIQA
printf 'm9qa:Qam9_123456
' | chpasswd
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:117:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:118\n'
(
cd /TMP/CLIQA
deluser m9qa
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:118:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:119\n'
(
cd /TMP/CLIQA
deluser SYSTEM
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:119:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:120\n'
(
cd /TMP/CLIQA
beep 2
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:120:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:121\n'
(
cd /TMP/CLIQA
fsync A.TXT
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:121:%s\n' "$_qa_result"
printf '__CLI_ee6430a441ba5690b0372e4a6799357d:B:122\n'
(
cd /TMP/CLIQA
sync
)
_qa_result=$?
printf '\n__CLI_ee6430a441ba5690b0372e4a6799357d:E:122:%s\n' "$_qa_result"
