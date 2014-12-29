#!/usr/bin/gnuplot -persist

# Ausgabe in PNG Datei
#set terminal png nocrop enhanced font "/usr/share/fonts/corefonts/verdana.ttf" 10 size 800,600
#set output 'messung.png'

set title "Messung"
set xdata time
set timefmt "%s"
set format x "%d.%m.%Y %H:%M"
set xtics nomirror rotate by -60
set grid

plot "messung.dat" using 1:2 with points lt 1 title "Temp / °C", \
     "messung.dat" using 1:3 with points lt 2 title "RLF / %"

# Darstellung des 24h-Verlaufs
#set format x "%H:%M"
#plot "messung.dat" using (int($1) % 86400):2 with dots lt 1 title "Temp / °C", \
#     "messung.dat" using (int($1) % 86400):3 with dots lt 2 title "RLF / %"
