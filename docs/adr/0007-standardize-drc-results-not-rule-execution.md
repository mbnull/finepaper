# Standardize DRC results rather than DRC rule execution

IP Core toolchains own and execute their semantic DRC using any implementation they require. Integration depends only on a versioned Diagnostic Report containing stable rule IDs, severity, messages, and design-subject references; the product does not define or execute a universal declarative DRC language.
