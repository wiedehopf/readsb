
cat > perf.sh << "EOF"
#!/bin/bash
perf record -F 99 --call-graph dwarf --pid $(pgrep -w -d ',' 'readsb')
EOF

{ timeout 120 ./perf.sh; }; sleep 2; perf script | ./stackcollapse-perf.pl --kernel | ./flamegraph.pl --width 1600 --bgcolors grey --cp > /opt/html/readsb.svg; echo; echo done

https://talawah.io/blog/extreme-http-performance-tuning-one-point-two-million/#flame-graph-generation
