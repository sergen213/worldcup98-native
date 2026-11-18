prog=currentProgram
listing=prog.getListing()
print([m for m in dir(listing) if not m.startswith('_')])
