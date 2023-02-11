
ci-version:
	@echo $(VERSION)

# Get the latest changelog entry without reformatting it
ci-changelog:
	@perl -pe 'exit if /^[^\s]/ && $$count++ > 0' Changes

# Get just the details from the changelog
ci-changelog-meat:
	@perl -ne 'next if !$$skipstart++; exit if /^ --/; print' Changes
