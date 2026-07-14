# Route unknown Package capabilities through generic or opaque fallback

An unknown business capability does not by itself reject a NoC Package. A capability using supported config/entity/relation schemas follows the generic Default Engine path; otherwise a namespaced extension is preserved and forwarded unchanged to IP Core tools as opaque, tool-managed data with no editable UI. Rejection is reserved for malformed core envelopes, missing required identity, duplicate IDs, invalid known-field types, unsafe executable declarations, or data claiming a known capability while violating its schema.
