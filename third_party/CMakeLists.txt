# ---------------------------------------------------------------------------
# Vendored third-party dependencies.
#
# Everything added here is committed to the repository so that a fresh clone
# builds without network access. Vendored sources link
# Velox::ThirdPartyOptions, which contributes no warning flags, so upstream
# code never breaks our build.
# ---------------------------------------------------------------------------

# The Vulkan Memory Allocator (VMA) is vendored here in Phase 1,
# together with the renderer that consumes it.