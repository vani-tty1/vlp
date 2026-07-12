man_page = configure_file(
    input: 'vlp.1.in',
    output: 'vlp.1',
    configuration: man_conf,
)
install_man(man_page)

vlp_sources = [
    'main.c',
    'vlp.c',
]

vlp_deps = [
    dependency('zlib'),
]

executable('vlp', vlp_sources,
    dependencies: vlp_deps,
    install: true,
)

