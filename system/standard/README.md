# PXA Standard System

Default composition root for the portable PXA System services. It owns and
wires the application registry, native runtime, runtime broker, Intent resolver,
task manager, role registry, theme service, renderer host, extensible service
registry, and bounded topic event broker.

Products may use this component as-is, configure capacities and policy, or
replace it with their own composition while keeping the same public contracts.
