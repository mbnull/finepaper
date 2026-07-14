# Keep the NoC internal transport opaque

The design tool does not model or expose the protocol used between internal NoC components. Internal coherence, transport, and routing representation remain private to the NoC Package and generator, while the public model describes only user intent, boundary interfaces, attachment, and supported configuration.
