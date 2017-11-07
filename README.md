# multiblock

## [+] Usage (need root privilege)

python hash-sort.py

iptables -A INPUT -j NFQUEUE --queue-num 0

iptables -A OUTPUT -j NFQUEUE --queue-num 0

make

./multiblock

## [+] Description

### Makefile

includes compile options

### hash-sort.py

hashes each line of top-1m.csv and sorts lines

### top-1m.csv

1 million top domain names

### top-1m-hash-sort.csv

hashed and sorted top-1m.csv
