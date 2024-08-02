project('dpdk_loopback',
        'c',
        version : '1.0.0',)

main_sources = files(
        'dpdk_loopback.c',
)

# Dependencies
pthread_dep = dependency('threads')
dpdk_dep = dependency('libdpdk')

executable(
    'dpdk_loopback', 
    sources : [main_sources, ],
    dependencies: [pthread_dep, dpdk_dep,],
)
