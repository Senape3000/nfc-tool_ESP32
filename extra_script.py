Import("env")

# Remove dimension checks for custom partition table
env.Replace(
    SIZECHECKCMD=""
)

print("✓ Program size check DISABLED - using custom partition table")
