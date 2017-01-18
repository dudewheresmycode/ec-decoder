{
    "targets": [
    {
        "target_name": "ec_decoder",
        "sources": [
            "src/main.cc",
            "src/decoder.cc"
        ],
        'include_dirs': [
            './usr/include',
            "<!(node -e \"require('nan')\")"
        ],
        "libraries": [
            "-L./usr/lib/",
            #"-L/usr/lib",
            "-lavformat","-lavformat","-lavfilter","-lavutil","-lavcodec","-lswscale","-lz", "-lm"
        ]
    }
    ]
}
