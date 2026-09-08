# System Services

Portable service contracts and reference providers belong here. Hardware and
vendor implementations remain in product repositories and register through
`pxsys_service_provider_t`.

Standard interface IDs use `system.*`. Vendor interfaces must use a controlled
reverse-domain namespace. Provider selection is versioned and policy-gated;
application runtime type is not part of the service namespace.
