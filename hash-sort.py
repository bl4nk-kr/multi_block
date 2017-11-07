import md5

def hash(plain):
	m = md5.new()
	m.update(plain)
	return m.hexdigest()

f1 = open("./top-1m.csv", "r")
f2 = open("./top-1m-hash-sort.csv", "w")

lines = f1.readlines()

hosts = []
for line in lines:
	line = line.split(",")[1][:-1]
	hosts.append(hash(line) + "\n")

hosts.sort()
for host in hosts:
	f2.write(host)
