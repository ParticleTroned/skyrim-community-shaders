# Material candidate: three repeats and interrupted run

All runs use gameft-sw with WPR active. Tables copy the saved final [50,60) statistics. Values in parentheses are deltas against the median of main-VR 503fbfc Balanced 1, 3 and 4. Original traced baseline is retained separately. Saves 08/09/10 use DLAA/native; 11/12/13 use DLSS with render scale.

The interrupted run contributes five completed saves only. Save 13 is MISSING, never zero. It is shown separately and is not included in the three-complete-repeat median. Its incomplete post-run provenance remains a limitation.

CPU 0.35 ms / GPU 0.15 ms are descriptive screening allowances, not confidence intervals. Reference and candidate ranges expose repeat variation.

**Active settings confound:** before/after receipts show FOV+TAA enabled with centre 0.60 in the 503fbfc Balanced references and 0.30 in the current repeats. The original traced baseline uses 0.30. These tables describe measured builds/settings; they do not isolate the material code change. See [settings audit](settings-audit.json). The pre-load quality mode also differs (1 versus 3); loaded-save modes are checked separately from their late health receipts.

## CPU mean (ms)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |      Current R1 |      Current R2 |     Interrupted |      Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | --------------: | --------------: | --------------: | --------------: | ------------------------------: | -----------------------: |
| 08   |                    4.664 |               5.757 [4.766,5.780] |  8.071 (+2.314) |  7.919 (+2.162) | 10.572 (+4.815) |  8.724 (+2.967) |    8.071 [7.919,8.724] (+2.314) |                   +3.407 |
| 09   |                    3.602 |               6.000 [5.937,6.116] |  6.192 (+0.192) |  6.297 (+0.297) |  6.583 (+0.583) |  6.494 (+0.494) |    6.297 [6.192,6.494] (+0.297) |                   +2.695 |
| 10   |                    2.937 |               6.433 [6.175,7.390] |  7.168 (+0.735) |  7.307 (+0.874) |  7.249 (+0.816) |  7.533 (+1.100) |    7.307 [7.168,7.533] (+0.874) |                   +4.370 |
| 11   |                    7.784 |               8.778 [8.753,8.782] |  8.504 (-0.274) |  8.641 (-0.137) |  9.198 (+0.420) |  8.957 (+0.179) |    8.641 [8.504,8.957] (-0.137) |                   +0.857 |
| 12   |                    6.854 |               8.826 [8.759,8.925] |  8.352 (-0.474) |  8.587 (-0.239) |  9.076 (+0.250) |  9.066 (+0.240) |    8.587 [8.352,9.066] (-0.239) |                   +1.733 |
| 13   |                    8.675 |            19.693 [13.410,19.769] | 18.355 (-1.338) | 17.645 (-2.048) |         MISSING | 19.661 (-0.032) | 18.355 [17.645,19.661] (-1.338) |                   +9.680 |

## GPU mean (ms)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |      Current R1 |      Current R2 |    Interrupted |      Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | --------------: | --------------: | -------------: | --------------: | ------------------------------: | -----------------------: |
| 08   |                   10.410 |               9.814 [9.270,9.857] |  9.289 (-0.525) |  9.351 (-0.463) | 9.359 (-0.455) |  9.197 (-0.617) |    9.289 [9.197,9.351] (-0.525) |                   -1.121 |
| 09   |                    9.537 |               7.965 [7.950,8.294] |  7.683 (-0.282) |  7.743 (-0.222) | 7.717 (-0.248) |  7.809 (-0.156) |    7.743 [7.683,7.809] (-0.222) |                   -1.794 |
| 10   |                   10.036 |               8.494 [8.361,9.244] |  8.784 (+0.290) |  8.736 (+0.242) | 8.690 (+0.196) |  8.691 (+0.197) |    8.736 [8.691,8.784] (+0.242) |                   -1.300 |
| 11   |                    7.835 |               8.195 [8.027,8.434] |  8.154 (-0.041) |  8.227 (+0.032) | 8.037 (-0.158) |  8.288 (+0.093) |    8.227 [8.154,8.288] (+0.032) |                   +0.392 |
| 12   |                    7.576 |               8.868 [8.797,9.248] |  8.652 (-0.216) |  8.724 (-0.144) | 8.819 (-0.049) |  8.927 (+0.059) |    8.724 [8.652,8.927] (-0.144) |                   +1.148 |
| 13   |                   10.259 |            13.128 [12.322,13.421] | 13.108 (-0.020) | 12.535 (-0.593) |        MISSING | 13.133 (+0.005) | 13.108 [12.535,13.133] (-0.020) |                   +2.849 |

## CPU P95 (ms)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |      Current R1 |      Current R2 |      Interrupted |      Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | --------------: | --------------: | ---------------: | --------------: | ------------------------------: | -----------------------: |
| 08   |                    5.200 |               6.100 [5.100,6.105] | 14.070 (+7.970) | 12.500 (+6.400) | 20.775 (+14.675) | 12.500 (+6.400) | 12.500 [12.500,14.070] (+6.400) |                   +7.300 |
| 09   |                    7.400 |               9.700 [9.600,9.800] |  9.900 (+0.200) | 10.100 (+0.400) |  10.200 (+0.500) | 10.300 (+0.600) |  10.100 [9.900,10.300] (+0.400) |                   +2.700 |
| 10   |                    3.200 |             10.400 [9.900,11.200] | 11.000 (+0.600) | 11.100 (+0.700) |  11.100 (+0.700) | 11.300 (+0.900) | 11.100 [11.000,11.300] (+0.700) |                   +7.900 |
| 11   |                   11.500 |            12.400 [12.200,12.700] | 12.500 (+0.100) | 12.785 (+0.385) |  14.800 (+2.400) | 13.020 (+0.620) | 12.785 [12.500,13.020] (+0.385) |                   +1.285 |
| 12   |                   10.600 |            12.900 [12.700,13.100] | 12.240 (-0.660) | 12.740 (-0.160) |  13.400 (+0.500) | 13.200 (+0.300) | 12.740 [12.240,13.200] (-0.160) |                   +2.140 |
| 13   |                   12.200 |            27.100 [22.300,27.400] | 26.550 (-0.550) | 24.800 (-2.300) |          MISSING | 27.015 (-0.085) | 26.550 [24.800,27.015] (-0.550) |                  +14.350 |

## CPU P99 (ms)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |       Current R1 |      Current R2 |      Interrupted |      Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | ---------------: | --------------: | ---------------: | --------------: | ------------------------------: | -----------------------: |
| 08   |                    9.212 |              8.417 [5.400,11.402] | 20.057 (+11.640) | 13.093 (+4.676) | 22.295 (+13.878) | 13.084 (+4.667) | 13.093 [13.084,20.057] (+4.676) |                   +3.881 |
| 09   |                    8.300 |            10.252 [10.171,10.341] |  10.200 (-0.052) | 10.400 (+0.148) |  10.800 (+0.548) | 10.700 (+0.448) | 10.400 [10.200,10.700] (+0.148) |                   +2.100 |
| 10   |                    3.400 |            10.800 [10.426,11.700] |  11.300 (+0.500) | 11.600 (+0.800) |  11.600 (+0.800) | 11.800 (+1.000) | 11.600 [11.300,11.800] (+0.800) |                   +8.200 |
| 11   |                   15.200 |            15.732 [14.632,16.000] |  16.200 (+0.468) | 16.714 (+0.982) |  20.302 (+4.570) | 17.104 (+1.372) | 16.714 [16.200,17.104] (+0.982) |                   +1.514 |
| 12   |                   13.315 |            15.000 [14.641,15.638] |  15.276 (+0.276) | 14.908 (-0.092) |  15.825 (+0.825) | 15.420 (+0.420) | 15.276 [14.908,15.420] (+0.276) |                   +1.961 |
| 13   |                   14.904 |            29.600 [26.417,29.860] |  30.250 (+0.650) | 27.040 (-2.560) |          MISSING | 29.629 (+0.029) | 29.629 [27.040,30.250] (+0.029) |                  +14.725 |

## GPU P95 (ms)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |      Current R1 |      Current R2 |     Interrupted |      Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | --------------: | --------------: | --------------: | --------------: | ------------------------------: | -----------------------: |
| 08   |                   10.600 |              9.900 [9.400,10.000] | 10.000 (+0.100) |  9.900 (+0.000) | 10.100 (+0.200) |  9.600 (-0.300) |   9.900 [9.600,10.000] (+0.000) |                   -0.700 |
| 09   |                   10.000 |               8.100 [8.100,8.400] |  7.800 (-0.300) |  7.900 (-0.200) |  7.900 (-0.200) |  8.000 (-0.100) |    7.900 [7.800,8.000] (-0.200) |                   -2.100 |
| 10   |                   10.200 |               8.600 [8.500,9.500] |  9.300 (+0.700) |  9.000 (+0.400) |  9.000 (+0.400) |  9.000 (+0.400) |    9.000 [9.000,9.300] (+0.400) |                   -1.200 |
| 11   |                    8.500 |               8.700 [8.200,8.705] |  8.500 (-0.200) |  8.800 (+0.100) |  9.770 (+1.070) |  8.920 (+0.220) |    8.800 [8.500,8.920] (+0.100) |                   +0.300 |
| 12   |                    8.000 |               9.100 [9.000,9.600] |  9.000 (-0.100) |  9.100 (+0.000) | 10.100 (+1.000) |  9.400 (+0.300) |    9.100 [9.000,9.400] (+0.000) |                   +1.100 |
| 13   |                   10.800 |            16.300 [14.885,16.900] | 16.350 (+0.050) | 15.300 (-1.000) |         MISSING | 16.300 (+0.000) | 16.300 [15.300,16.350] (+0.000) |                   +5.500 |

## GPU P99 (ms)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |      Current R1 |      Current R2 |     Interrupted |      Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | --------------: | --------------: | --------------: | --------------: | ------------------------------: | -----------------------: |
| 08   |                   10.900 |             10.000 [9.400,10.000] | 10.457 (+0.457) | 10.000 (+0.000) | 10.400 (+0.400) |  9.800 (-0.200) |  10.000 [9.800,10.457] (+0.000) |                   -0.900 |
| 09   |                   10.200 |               8.200 [8.100,8.500] |  7.900 (-0.300) |  7.900 (-0.300) |  8.200 (+0.000) |  8.100 (-0.100) |    7.900 [7.900,8.100] (-0.300) |                   -2.300 |
| 10   |                   10.300 |               8.900 [8.800,9.600] |  9.500 (+0.600) |  9.290 (+0.390) |  9.360 (+0.460) |  9.300 (+0.400) |    9.300 [9.290,9.500] (+0.400) |                   -1.000 |
| 11   |                    9.906 |             10.832 [9.800,12.282] | 12.120 (+1.288) | 12.914 (+2.082) | 15.334 (+4.502) | 13.308 (+2.476) | 12.914 [12.120,13.308] (+2.082) |                   +3.008 |
| 12   |                    8.200 |            10.900 [10.514,11.250] |  9.900 (-1.000) | 11.200 (+0.300) | 12.425 (+1.525) | 10.720 (-0.180) |  10.720 [9.900,11.200] (-0.180) |                   +2.520 |
| 13   |                   12.204 |            18.500 [17.819,19.300] | 18.800 (+0.300) | 17.140 (-1.360) |         MISSING | 18.100 (-0.400) | 18.100 [17.140,18.800] (-0.400) |                   +5.896 |

## CPU isolated spikes/s

| Save | Original traced baseline | Main-VR Balanced median [min,max] |      Current R1 |       Current R2 |     Interrupted |       Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | --------------: | ---------------: | --------------: | ---------------: | ------------------------------: | -----------------------: |
| 08   |                    0.200 |               0.000 [0.000,0.100] |  4.300 (+4.300) | 18.200 (+18.200) |  3.400 (+3.400) | 17.600 (+17.600) | 17.600 [4.300,18.200] (+17.600) |                  +17.400 |
| 09   |                    5.700 |               0.700 [0.700,8.800] |  0.400 (-0.300) |   0.400 (-0.300) |  1.100 (+0.400) |   0.200 (-0.500) |    0.400 [0.200,0.400] (-0.300) |                   -5.300 |
| 10   |                    0.000 |            14.900 [14.600,25.800] | 24.600 (+9.700) |  24.800 (+9.900) | 22.000 (+7.100) |  24.400 (+9.500) | 24.600 [24.400,24.800] (+9.700) |                  +24.600 |
| 11   |                    5.900 |               5.200 [3.600,6.000] |  3.600 (-1.600) |   5.400 (+0.200) | 13.600 (+8.400) |   9.300 (+4.100) |    5.400 [3.600,9.300] (+0.200) |                   -0.500 |
| 12   |                    4.700 |              8.900 [7.600,12.500] |  7.400 (-1.500) |   8.300 (-0.600) | 15.500 (+6.600) |  12.300 (+3.400) |   8.300 [7.400,12.300] (-0.600) |                   +3.600 |
| 13   |                    4.400 |               0.500 [0.200,0.700] |  0.900 (+0.400) |   0.300 (-0.200) |         MISSING |   0.500 (+0.000) |    0.500 [0.300,0.900] (+0.000) |                   -3.900 |

## GPU isolated spikes/s

| Save | Original traced baseline | Main-VR Balanced median [min,max] |     Current R1 |     Current R2 |    Interrupted |     Current R3 |     Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | -------------: | -------------: | -------------: | -------------: | ---------------------------: | -----------------------: |
| 08   |                    0.100 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -0.100 |
| 09   |                    0.100 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -0.100 |
| 10   |                    0.100 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -0.100 |
| 11   |                    1.200 |               1.300 [0.700,3.900] | 3.400 (+2.100) | 3.900 (+2.600) | 3.200 (+1.900) | 3.900 (+2.600) | 3.900 [3.400,3.900] (+2.600) |                   +2.700 |
| 12   |                    0.100 |               1.000 [0.800,1.200] | 0.900 (-0.100) | 1.400 (+0.400) | 4.000 (+3.000) | 1.000 (+0.000) | 1.000 [0.900,1.400] (+0.000) |                   +0.900 |
| 13   |                    0.600 |               5.900 [3.000,6.600] | 6.300 (+0.400) | 5.500 (-0.400) |        MISSING | 7.000 (+1.100) | 6.300 [5.500,7.000] (+0.400) |                   +5.700 |

## CPU spike groups/s

| Save | Original traced baseline | Main-VR Balanced median [min,max] |       Current R1 |       Current R2 |     Interrupted |       Current R3 |         Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | ---------------: | ---------------: | --------------: | ---------------: | -------------------------------: | -----------------------: |
| 08   |                    0.500 |               0.100 [0.100,0.200] | 10.200 (+10.100) | 21.400 (+21.300) |  5.800 (+5.700) | 18.600 (+18.500) | 18.600 [10.200,21.400] (+18.500) |                  +18.100 |
| 09   |                   10.600 |            11.700 [11.200,15.000] |   8.600 (-3.100) |   9.200 (-2.500) |  9.800 (-1.900) |  10.000 (-1.700) |    9.200 [8.600,10.000] (-2.500) |                   -1.400 |
| 10   |                    0.000 |            21.400 [20.000,25.800] |  24.900 (+3.500) |  25.300 (+3.900) | 24.200 (+2.800) |  24.900 (+3.500) |  24.900 [24.900,25.300] (+3.500) |                  +24.900 |
| 11   |                   13.800 |             12.200 [8.700,12.800] |  11.300 (-0.900) |  12.600 (+0.400) | 16.900 (+4.700) |  17.500 (+5.300) |  12.600 [11.300,17.500] (+0.400) |                   -1.200 |
| 12   |                   14.400 |            16.200 [15.200,17.900] |  15.000 (-1.200) |  15.400 (-0.800) | 20.300 (+4.100) |  18.600 (+2.400) |  15.400 [15.000,18.600] (-0.800) |                   +1.000 |
| 13   |                    7.100 |               3.700 [2.400,6.000] |   5.200 (+1.500) |   1.700 (-2.000) |         MISSING |   4.600 (+0.900) |     4.600 [1.700,5.200] (+0.900) |                   -2.500 |

## GPU spike groups/s

| Save | Original traced baseline | Main-VR Balanced median [min,max] |     Current R1 |     Current R2 |    Interrupted |     Current R3 |     Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | -------------: | -------------: | -------------: | -------------: | ---------------------------: | -----------------------: |
| 08   |                    0.100 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -0.100 |
| 09   |                    0.100 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -0.100 |
| 10   |                    0.100 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -0.100 |
| 11   |                    1.200 |               1.300 [0.700,3.900] | 3.400 (+2.100) | 3.900 (+2.600) | 3.200 (+1.900) | 3.900 (+2.600) | 3.900 [3.400,3.900] (+2.600) |                   +2.700 |
| 12   |                    0.100 |               1.000 [0.800,1.200] | 0.900 (-0.100) | 1.400 (+0.400) | 4.000 (+3.000) | 1.000 (+0.000) | 1.000 [0.900,1.400] (+0.000) |                   +0.900 |
| 13   |                    0.700 |               6.400 [3.400,6.800] | 7.300 (+0.900) | 5.600 (-0.800) |        MISSING | 7.700 (+1.300) | 7.300 [5.600,7.700] (+0.900) |                   +6.600 |

## CPU spike duty (%)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |       Current R1 |       Current R2 |      Interrupted |       Current R3 |         Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | ---------------: | ---------------: | ---------------: | ---------------: | -------------------------------: | -----------------------: |
| 08   |                    2.170 |               1.170 [0.170,2.500] | 34.470 (+33.300) | 35.030 (+33.860) | 33.660 (+32.490) | 27.890 (+26.720) | 34.470 [27.890,35.030] (+33.300) |                  +32.300 |
| 09   |                   29.620 |            23.690 [23.440,25.450] |  24.440 (+0.750) |  25.200 (+1.510) |  25.630 (+1.940) |  25.180 (+1.490) |  25.180 [24.440,25.200] (+1.490) |                   -4.440 |
| 10   |                    0.000 |            28.780 [25.850,28.860] |  27.810 (-0.970) |  28.210 (-0.570) |  28.060 (-0.720) |  27.680 (-1.100) |  27.810 [27.680,28.210] (-0.970) |                  +27.810 |
| 11   |                   25.290 |            24.620 [24.600,26.390] |  28.120 (+3.500) |  26.920 (+2.300) |  30.120 (+5.500) |  27.680 (+3.060) |  27.680 [26.920,28.120] (+3.060) |                   +2.390 |
| 12   |                   27.320 |            26.020 [25.620,29.080] |  26.160 (+0.140) |  25.680 (-0.340) |  25.920 (-0.100) |  25.480 (-0.540) |  25.680 [25.480,26.160] (-0.340) |                   -1.640 |
| 13   |                   18.700 |            36.190 [30.820,39.370] |  37.390 (+1.200) |  39.930 (+3.740) |          MISSING |  35.660 (-0.530) |  37.390 [35.660,39.930] (+1.200) |                  +18.690 |

## GPU spike duty (%)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |      Current R1 |     Current R2 |    Interrupted |      Current R3 |       Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | --------------: | -------------: | -------------: | --------------: | -----------------------------: | -----------------------: |
| 08   |                    0.170 |               0.000 [0.000,0.000] |  0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) |  0.000 (+0.000) |   0.000 [0.000,0.000] (+0.000) |                   -0.170 |
| 09   |                    0.160 |               0.000 [0.000,0.000] |  0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) |  0.000 (+0.000) |   0.000 [0.000,0.000] (+0.000) |                   -0.160 |
| 10   |                    0.170 |               0.000 [0.000,0.000] |  0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) |  0.000 (+0.000) |   0.000 [0.000,0.000] (+0.000) |                   -0.170 |
| 11   |                    1.150 |               1.220 [0.630,3.680] |  3.150 (+1.930) | 3.740 (+2.520) | 4.170 (+2.950) |  3.910 (+2.690) |   3.740 [3.150,3.910] (+2.520) |                   +2.590 |
| 12   |                    0.090 |               1.100 [0.830,1.250] |  0.890 (-0.210) | 1.410 (+0.310) | 4.100 (+3.000) |  1.020 (-0.080) |   1.020 [0.890,1.410] (-0.080) |                   +0.930 |
| 13   |                    1.340 |             12.380 [9.080,12.480] | 16.700 (+4.320) | 9.810 (-2.570) |        MISSING | 15.770 (+3.390) | 15.770 [9.810,16.700] (+3.390) |                  +14.430 |

## CPU largest group (samples)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |       Current R1 |       Current R2 |      Interrupted |      Current R3 |        Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | ---------------: | ---------------: | ---------------: | --------------: | ------------------------------: | -----------------------: |
| 08   |                    4.000 |               7.000 [1.000,8.000] | 19.000 (+12.000) |   5.000 (-2.000) | 19.000 (+12.000) |  5.000 (-2.000) |   5.000 [5.000,19.000] (-2.000) |                   +1.000 |
| 09   |                    6.000 |               4.000 [4.000,6.000] |   5.000 (+1.000) |   5.000 (+1.000) |   7.000 (+3.000) |  4.000 (+0.000) |    5.000 [4.000,5.000] (+1.000) |                   -1.000 |
| 10   |                    0.000 |               2.000 [1.000,3.000] |   2.000 (+0.000) |   2.000 (+0.000) |   2.000 (+0.000) |  2.000 (+0.000) |    2.000 [2.000,2.000] (+0.000) |                   +2.000 |
| 11   |                    8.000 |              9.000 [7.000,14.000] |   8.000 (-1.000) |  11.000 (+2.000) |   9.000 (+0.000) |  5.000 (-4.000) |   8.000 [5.000,11.000] (-1.000) |                   +0.000 |
| 12   |                    6.000 |               5.000 [3.000,6.000] |   5.000 (+0.000) |   4.000 (-1.000) |   3.000 (-2.000) |  3.000 (-2.000) |    4.000 [3.000,5.000] (-1.000) |                   -2.000 |
| 13   |                    5.000 |             14.000 [8.000,45.000] |  13.000 (-1.000) | 79.000 (+65.000) |          MISSING | 13.000 (-1.000) | 13.000 [13.000,79.000] (-1.000) |                   +8.000 |

## GPU largest group (samples)

| Save | Original traced baseline | Main-VR Balanced median [min,max] |     Current R1 |     Current R2 |    Interrupted |     Current R3 |     Current median [min,max] | Median delta vs original |
| ---- | -----------------------: | --------------------------------: | -------------: | -------------: | -------------: | -------------: | ---------------------------: | -----------------------: |
| 08   |                    1.000 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -1.000 |
| 09   |                    1.000 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -1.000 |
| 10   |                    1.000 |               0.000 [0.000,0.000] | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 (+0.000) | 0.000 [0.000,0.000] (+0.000) |                   -1.000 |
| 11   |                    1.000 |               1.000 [1.000,1.000] | 1.000 (+0.000) | 1.000 (+0.000) | 1.000 (+0.000) | 1.000 (+0.000) | 1.000 [1.000,1.000] (+0.000) |                   +0.000 |
| 12   |                    1.000 |               1.000 [1.000,1.000] | 1.000 (+0.000) | 1.000 (+0.000) | 1.000 (+0.000) | 1.000 (+0.000) | 1.000 [1.000,1.000] (+0.000) |                   +0.000 |
| 13   |                    2.000 |              2.000 [2.000,13.000] | 5.000 (+3.000) | 2.000 (+0.000) |        MISSING | 5.000 (+3.000) | 5.000 [2.000,5.000] (+3.000) |                   +3.000 |

## Settling and health

Timing stability and render recovery are different. Below are the unchanged saved CPU baseline/GPU settling fields and full lifecycle summaries. CPU zero is displayed as the first five-second window; it is not proof of instantaneous settling.

### Current R1

| Save | CPU baseline stability     | GPU settling | Latch metric                     | Lifecycle and end health                                                                                                                                                                                                                                                                                             |
| ---- | -------------------------- | ------------ | -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 08   | Not established by 60 s    | 23 s         | 305 frame(s), 5966.9 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (305f/5966.9ms); no stretch observed; final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt                                         |
| 09   | From first 5-second window | 0 s          | 347 frame(s), 3354.2 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (347f/3354.2ms); stretch started by +1s and completed by +5s (318f/6138.4ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 10   | From first 5-second window | 0 s          | 246 frame(s), 3259.8 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (246f/3259.8ms); stretch started by +1s and completed by +5s (312f/6107.4ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 11   | Not established by 60 s    | 41 s         | 169 frame(s), 2613.1 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (169f/2613.1ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                        |
| 12   | 3 s                        | 2 s          | 255 frame(s), 3262.4 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (255f/3262.4ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                        |
| 13   | Not established by 60 s    | 50 s         | 21 frame(s), 1047.6 ms, DLSS/q1  | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (21f/1047.6ms); stretch started by +5s and completed by +5s (1f/133.9ms); final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                    |

### Current R2

| Save | CPU baseline stability     | GPU settling | Latch metric                     | Lifecycle and end health                                                                                                                                                                                                                                                                                             |
| ---- | -------------------------- | ------------ | -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 08   | Not established by 60 s    | 0 s          | 320 frame(s), 5984.9 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (320f/5984.9ms); no stretch observed; final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt                                         |
| 09   | From first 5-second window | 1 s          | 350 frame(s), 3348.7 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (350f/3348.7ms); stretch started by +1s and completed by +5s (374f/6635.9ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 10   | From first 5-second window | 1 s          | 261 frame(s), 3246.6 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (261f/3246.6ms); stretch started by +1s and completed by +5s (329f/6238.1ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 11   | 2 s                        | 2 s          | 164 frame(s), 2574 ms, DLSS/q1   | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (164f/2574ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                          |
| 12   | 11 s                       | 2 s          | 235 frame(s), 3256.1 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (235f/3256.1ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                        |
| 13   | Not established by 60 s    | 48 s         | 133 frame(s), 3257.2 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (133f/3257.2ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                        |

### Current interrupted

| Save | CPU baseline stability     | GPU settling            | Latch metric                     | Lifecycle and end health                                                                                                                                                                                                                                                                                             |
| ---- | -------------------------- | ----------------------- | -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 08   | Not established by 60 s    | 0 s                     | 323 frame(s), 5980.2 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (323f/5980.2ms); no stretch observed; final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt                                         |
| 09   | Not established by 60 s    | 47 s                    | 172 frame(s), 3311.6 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (172f/3311.6ms); stretch started by +1s and completed by +5s (495f/7630.8ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 10   | From first 5-second window | 26 s                    | 231 frame(s), 3260.6 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (231f/3260.6ms); stretch started by +1s and completed by +5s (330f/6258.4ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 11   | 31 s                       | Not established by 60 s | 135 frame(s), 2758.8 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (135f/2758.8ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                        |
| 12   | From first 5-second window | 4 s                     | 218 frame(s), 3231 ms, DLSS/q1   | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (218f/3231ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                          |
| 13   | MISSING                    | MISSING                 | Incomplete                       | Final 60-second health missing                                                                                                                                                                                                                                                                                       |

### Current R3

| Save | CPU baseline stability     | GPU settling | Latch metric                     | Lifecycle and end health                                                                                                                                                                                                                                                                                             |
| ---- | -------------------------- | ------------ | -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 08   | 15 s                       | 0 s          | 310 frame(s), 5970.6 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (310f/5970.6ms); no stretch observed; final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt                                         |
| 09   | From first 5-second window | 1 s          | 349 frame(s), 3350.4 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (349f/3350.4ms); stretch started by +1s and completed by +5s (355f/6445.3ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 10   | From first 5-second window | 1 s          | 242 frame(s), 3273.5 ms, None/q0 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (242f/3273.5ms); stretch started by +1s and completed by +5s (328f/6236.2ms); final +60s successful: Native/no render-scale, stretch inactive, stereo complete, no owner/retirement debt |
| 11   | 2 s                        | 2 s          | 163 frame(s), 2645.1 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (163f/2645.1ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                        |
| 12   | 1 s                        | 4 s          | 225 frame(s), 3272.8 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (225f/3272.8ms); no stretch observed; final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                                                        |
| 13   | Not established by 60 s    | 44 s         | 130 frame(s), 3360.1 ms, DLSS/q1 | Native/no render-scale observed by +1s; mode already applied at first health sample; completed latch metric observed by +5s (130f/3360.1ms); stretch started by +5s and completed by +5s (1f/129.2ms); final +60s successful: DLSS/q1, stretch inactive, stereo complete, no owner/retirement debt                   |

[Saved timing/deltas](timing-comparison.json), [machine-readable deltas](timing-deltas.csv), [earlier eleven-run comparison](../rc166-comparison-20260916/tables.md). WPR attribution is a separate analysis; this timing table does not establish a material-cost saving.
