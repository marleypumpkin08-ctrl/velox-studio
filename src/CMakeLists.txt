# ---------------------------------------------------------------------------
# Velox Studio application modules.
#
# Each module is a static library with a narrow public interface so the
# dependency graph stays acyclic and modules remain independently testable.
# ---------------------------------------------------------------------------

add_subdirectory(core)
add_subdirectory(document)
add_subdirectory(io)
add_subdirectory(render)
add_subdirectory(update)
add_subdirectory(ui)
add_subdirectory(app)