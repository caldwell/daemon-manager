
ci-version:
	@echo $(VERSION)

# Get the latest changelog entry without reformatting it
ci-changelog:
	@perl -pe 'exit if /^[^\s]/ && $$count++ > 0' Changes
