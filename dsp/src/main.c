/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <stdio.h>
 #include <stddef.h>
 #include <errno.h>
 #include <stdlib.h>
 #include "arm_math.h"
 #include "arm_const_structs.h"
 #include <zephyr/kernel.h>
 #include <zephyr/audio/dmic.h>
 #include <zephyr/drivers/gpio.h>
 #include <zephyr/drivers/gpio/gpio_sx1509b.h>
 #include <zephyr/logging/log.h>
 #include <zephyr/kernel.h>
 #include <zephyr/sys/printk.h>
 #include <zephyr/sys/byteorder.h>
 #include <zephyr/sys/ring_buffer.h>
 #include "bluetooth.h"
 
 LOG_MODULE_REGISTER(thingy52_node);
  
  #define MAX_SAMPLE_RATE  16000
  #define SAMPLE_BIT_WIDTH 16
  #define BYTES_PER_SAMPLE sizeof(int16_t)
 
  /* Milliseconds to wait for a block to be read. */
  #define READ_TIMEOUT     1000
  #define READ_DELAY_MS 50
 
 #define BLE_CHUNK_DATA_LEN 19
 #define BLE_CHUNK_TOTAL    20 
  
  /* Size of a block for 100 ms of audio data. */
  #define BLOCK_SIZE(_sample_rate, _number_of_channels) \
      (BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)
  
  #define MAX_BLOCK_SIZE BLOCK_SIZE(MAX_SAMPLE_RATE, 1)/2
  #define BLOCK_COUNT 2
  #define SLAB_DEPTH (BLOCK_COUNT * 4)
 
 /* FFT parameters */
 #define FFT_LEN 1024
 static float32_t mono_f32[FFT_LEN];
 static float32_t cbuf[2*FFT_LEN];
 static float32_t mag[FFT_LEN];
 static const float32_t hann[FFT_LEN] = {
    0.00000000e+00f, 9.41235870e-06f, 3.76490804e-05f, 8.47091021e-05f, 1.50590652e-04f, 2.35291249e-04f, 3.38807706e-04f, 4.61136124e-04f,
    6.02271897e-04f, 7.62209713e-04f, 9.40943550e-04f, 1.13846668e-03f, 1.35477166e-03f, 1.58985035e-03f, 1.84369391e-03f, 2.11629277e-03f,
    2.40763666e-03f, 2.71771463e-03f, 3.04651500e-03f, 3.39402538e-03f, 3.76023270e-03f, 4.14512317e-03f, 4.54868229e-03f, 4.97089487e-03f,
    5.41174502e-03f, 5.87121613e-03f, 6.34929092e-03f, 6.84595138e-03f, 7.36117881e-03f, 7.89495381e-03f, 8.44725628e-03f, 9.01806545e-03f,
    9.60735980e-03f, 1.02151172e-02f, 1.08413146e-02f, 1.14859287e-02f, 1.21489350e-02f, 1.28303086e-02f, 1.35300239e-02f, 1.42480545e-02f,
    1.49843734e-02f, 1.57389529e-02f, 1.65117645e-02f, 1.73027792e-02f, 1.81119671e-02f, 1.89392979e-02f, 1.97847403e-02f, 2.06482626e-02f,
    2.15298321e-02f, 2.24294158e-02f, 2.33469798e-02f, 2.42824895e-02f, 2.52359097e-02f, 2.62072045e-02f, 2.71963373e-02f, 2.82032709e-02f,
    2.92279674e-02f, 3.02703882e-02f, 3.13304940e-02f, 3.24082450e-02f, 3.35036006e-02f, 3.46165195e-02f, 3.57469598e-02f, 3.68948789e-02f,
    3.80602337e-02f, 3.92429803e-02f, 4.04430742e-02f, 4.16604700e-02f, 4.28951221e-02f, 4.41469840e-02f, 4.54160085e-02f, 4.67021477e-02f,
    4.80053534e-02f, 4.93255765e-02f, 5.06627672e-02f, 5.20168751e-02f, 5.33878494e-02f, 5.47756384e-02f, 5.61801898e-02f, 5.76014508e-02f,
    5.90393678e-02f, 6.04938868e-02f, 6.19649529e-02f, 6.34525108e-02f, 6.49565044e-02f, 6.64768772e-02f, 6.80135719e-02f, 6.95665307e-02f,
    7.11356950e-02f, 7.27210058e-02f, 7.43224034e-02f, 7.59398276e-02f, 7.75732174e-02f, 7.92225113e-02f, 8.08876472e-02f, 8.25685625e-02f,
    8.42651938e-02f, 8.59774774e-02f, 8.77053486e-02f, 8.94487425e-02f, 9.12075934e-02f, 9.29818351e-02f, 9.47714009e-02f, 9.65762232e-02f,
    9.83962343e-02f, 1.00231365e-01f, 1.02081548e-01f, 1.03946711e-01f, 1.05826786e-01f, 1.07721701e-01f, 1.09631386e-01f, 1.11555767e-01f,
    1.13494773e-01f, 1.15448331e-01f, 1.17416367e-01f, 1.19398807e-01f, 1.21395577e-01f, 1.23406600e-01f, 1.25431803e-01f, 1.27471107e-01f,
    1.29524437e-01f, 1.31591716e-01f, 1.33672864e-01f, 1.35767805e-01f, 1.37876459e-01f, 1.39998746e-01f, 1.42134587e-01f, 1.44283902e-01f,
    1.46446609e-01f, 1.48622628e-01f, 1.50811875e-01f, 1.53014270e-01f, 1.55229728e-01f, 1.57458166e-01f, 1.59699501e-01f, 1.61953648e-01f,
    1.64220523e-01f, 1.66500039e-01f, 1.68792111e-01f, 1.71096653e-01f, 1.73413579e-01f, 1.75742799e-01f, 1.78084229e-01f, 1.80437778e-01f,
    1.82803358e-01f, 1.85180881e-01f, 1.87570256e-01f, 1.89971394e-01f, 1.92384205e-01f, 1.94808597e-01f, 1.97244479e-01f, 1.99691760e-01f,
    2.02150348e-01f, 2.04620149e-01f, 2.07101071e-01f, 2.09593021e-01f, 2.12095904e-01f, 2.14609627e-01f, 2.17134095e-01f, 2.19669212e-01f,
    2.22214883e-01f, 2.24771014e-01f, 2.27337506e-01f, 2.29914264e-01f, 2.32501190e-01f, 2.35098188e-01f, 2.37705159e-01f, 2.40322005e-01f,
    2.42948628e-01f, 2.45584929e-01f, 2.48230808e-01f, 2.50886167e-01f, 2.53550904e-01f, 2.56224920e-01f, 2.58908114e-01f, 2.61600385e-01f,
    2.64301632e-01f, 2.67011752e-01f, 2.69730645e-01f, 2.72458206e-01f, 2.75194335e-01f, 2.77938928e-01f, 2.80691881e-01f, 2.83453091e-01f,
    2.86222453e-01f, 2.88999865e-01f, 2.91785220e-01f, 2.94578414e-01f, 2.97379343e-01f, 3.00187900e-01f, 3.03003980e-01f, 3.05827477e-01f,
    3.08658284e-01f, 3.11496295e-01f, 3.14341403e-01f, 3.17193501e-01f, 3.20052482e-01f, 3.22918237e-01f, 3.25790660e-01f, 3.28669641e-01f,
    3.31555073e-01f, 3.34446847e-01f, 3.37344854e-01f, 3.40248985e-01f, 3.43159130e-01f, 3.46075180e-01f, 3.48997025e-01f, 3.51924556e-01f,
    3.54857661e-01f, 3.57796231e-01f, 3.60740155e-01f, 3.63689322e-01f, 3.66643621e-01f, 3.69602941e-01f, 3.72567170e-01f, 3.75536197e-01f,
    3.78509910e-01f, 3.81488197e-01f, 3.84470946e-01f, 3.87458044e-01f, 3.90449380e-01f, 3.93444840e-01f, 3.96444312e-01f, 3.99447683e-01f,
    4.02454839e-01f, 4.05465668e-01f, 4.08480056e-01f, 4.11497890e-01f, 4.14519056e-01f, 4.17543440e-01f, 4.20570928e-01f, 4.23601407e-01f,
    4.26634763e-01f, 4.29670880e-01f, 4.32709646e-01f, 4.35750945e-01f, 4.38794662e-01f, 4.41840685e-01f, 4.44888896e-01f, 4.47939183e-01f,
    4.50991430e-01f, 4.54045522e-01f, 4.57101344e-01f, 4.60158781e-01f, 4.63217718e-01f, 4.66278040e-01f, 4.69339632e-01f, 4.72402378e-01f,
    4.75466163e-01f, 4.78530872e-01f, 4.81596389e-01f, 4.84662598e-01f, 4.87729386e-01f, 4.90796635e-01f, 4.93864231e-01f, 4.96932058e-01f,
    5.00000000e-01f, 5.03067942e-01f, 5.06135769e-01f, 5.09203365e-01f, 5.12270614e-01f, 5.15337402e-01f, 5.18403611e-01f, 5.21469128e-01f,
    5.24533837e-01f, 5.27597622e-01f, 5.30660368e-01f, 5.33721960e-01f, 5.36782282e-01f, 5.39841219e-01f, 5.42898656e-01f, 5.45954478e-01f,
    5.49008570e-01f, 5.52060817e-01f, 5.55111104e-01f, 5.58159315e-01f, 5.61205338e-01f, 5.64249055e-01f, 5.67290354e-01f, 5.70329120e-01f,
    5.73365237e-01f, 5.76398593e-01f, 5.79429072e-01f, 5.82456560e-01f, 5.85480944e-01f, 5.88502110e-01f, 5.91519944e-01f, 5.94534332e-01f,
    5.97545161e-01f, 6.00552317e-01f, 6.03555688e-01f, 6.06555160e-01f, 6.09550620e-01f, 6.12541956e-01f, 6.15529054e-01f, 6.18511803e-01f,
    6.21490090e-01f, 6.24463803e-01f, 6.27432830e-01f, 6.30397059e-01f, 6.33356379e-01f, 6.36310678e-01f, 6.39259845e-01f, 6.42203769e-01f,
    6.45142339e-01f, 6.48075444e-01f, 6.51002975e-01f, 6.53924820e-01f, 6.56840870e-01f, 6.59751015e-01f, 6.62655146e-01f, 6.65553153e-01f,
    6.68444927e-01f, 6.71330359e-01f, 6.74209340e-01f, 6.77081763e-01f, 6.79947518e-01f, 6.82806499e-01f, 6.85658597e-01f, 6.88503705e-01f,
    6.91341716e-01f, 6.94172523e-01f, 6.96996020e-01f, 6.99812100e-01f, 7.02620657e-01f, 7.05421586e-01f, 7.08214780e-01f, 7.11000135e-01f,
    7.13777547e-01f, 7.16546909e-01f, 7.19308119e-01f, 7.22061072e-01f, 7.24805665e-01f, 7.27541794e-01f, 7.30269355e-01f, 7.32988248e-01f,
    7.35698368e-01f, 7.38399615e-01f, 7.41091886e-01f, 7.43775080e-01f, 7.46449096e-01f, 7.49113833e-01f, 7.51769192e-01f, 7.54415071e-01f,
    7.57051372e-01f, 7.59677995e-01f, 7.62294841e-01f, 7.64901812e-01f, 7.67498810e-01f, 7.70085736e-01f, 7.72662494e-01f, 7.75228986e-01f,
    7.77785117e-01f, 7.80330788e-01f, 7.82865905e-01f, 7.85390373e-01f, 7.87904096e-01f, 7.90406979e-01f, 7.92898929e-01f, 7.95379851e-01f,
    7.97849652e-01f, 8.00308240e-01f, 8.02755521e-01f, 8.05191403e-01f, 8.07615795e-01f, 8.10028606e-01f, 8.12429744e-01f, 8.14819119e-01f,
    8.17196642e-01f, 8.19562222e-01f, 8.21915771e-01f, 8.24257201e-01f, 8.26586421e-01f, 8.28903347e-01f, 8.31207889e-01f, 8.33499961e-01f,
    8.35779477e-01f, 8.38046352e-01f, 8.40300499e-01f, 8.42541834e-01f, 8.44770272e-01f, 8.46985730e-01f, 8.49188125e-01f, 8.51377372e-01f,
    8.53553391e-01f, 8.55716098e-01f, 8.57865413e-01f, 8.60001254e-01f, 8.62123541e-01f, 8.64232195e-01f, 8.66327136e-01f, 8.68408284e-01f,
    8.70475563e-01f, 8.72528893e-01f, 8.74568197e-01f, 8.76593400e-01f, 8.78604423e-01f, 8.80601193e-01f, 8.82583633e-01f, 8.84551669e-01f,
    8.86505227e-01f, 8.88444233e-01f, 8.90368614e-01f, 8.92278299e-01f, 8.94173214e-01f, 8.96053289e-01f, 8.97918452e-01f, 8.99768635e-01f,
    9.01603766e-01f, 9.03423777e-01f, 9.05228599e-01f, 9.07018165e-01f, 9.08792407e-01f, 9.10551257e-01f, 9.12294651e-01f, 9.14022523e-01f,
    9.15734806e-01f, 9.17431437e-01f, 9.19112353e-01f, 9.20777489e-01f, 9.22426783e-01f, 9.24060172e-01f, 9.25677597e-01f, 9.27278994e-01f,
    9.28864305e-01f, 9.30433469e-01f, 9.31986428e-01f, 9.33523123e-01f, 9.35043496e-01f, 9.36547489e-01f, 9.38035047e-01f, 9.39506113e-01f,
    9.40960632e-01f, 9.42398549e-01f, 9.43819810e-01f, 9.45224362e-01f, 9.46612151e-01f, 9.47983125e-01f, 9.49337233e-01f, 9.50674424e-01f,
    9.51994647e-01f, 9.53297852e-01f, 9.54583992e-01f, 9.55853016e-01f, 9.57104878e-01f, 9.58339530e-01f, 9.59556926e-01f, 9.60757020e-01f,
    9.61939766e-01f, 9.63105121e-01f, 9.64253040e-01f, 9.65383481e-01f, 9.66496399e-01f, 9.67591755e-01f, 9.68669506e-01f, 9.69729612e-01f,
    9.70772033e-01f, 9.71796729e-01f, 9.72803663e-01f, 9.73792796e-01f, 9.74764090e-01f, 9.75717510e-01f, 9.76653020e-01f, 9.77570584e-01f,
    9.78470168e-01f, 9.79351737e-01f, 9.80215260e-01f, 9.81060702e-01f, 9.81888033e-01f, 9.82697221e-01f, 9.83488236e-01f, 9.84261047e-01f,
    9.85015627e-01f, 9.85751945e-01f, 9.86469976e-01f, 9.87169691e-01f, 9.87851065e-01f, 9.88514071e-01f, 9.89158685e-01f, 9.89784883e-01f,
    9.90392640e-01f, 9.90981935e-01f, 9.91552744e-01f, 9.92105046e-01f, 9.92638821e-01f, 9.93154049e-01f, 9.93650709e-01f, 9.94128784e-01f,
    9.94588255e-01f, 9.95029105e-01f, 9.95451318e-01f, 9.95854877e-01f, 9.96239767e-01f, 9.96605975e-01f, 9.96953485e-01f, 9.97282285e-01f,
    9.97592363e-01f, 9.97883707e-01f, 9.98156306e-01f, 9.98410150e-01f, 9.98645228e-01f, 9.98861533e-01f, 9.99059056e-01f, 9.99237790e-01f,
    9.99397728e-01f, 9.99538864e-01f, 9.99661192e-01f, 9.99764709e-01f, 9.99849409e-01f, 9.99915291e-01f, 9.99962351e-01f, 9.99990588e-01f,
    1.00000000e+00f, 9.99990588e-01f, 9.99962351e-01f, 9.99915291e-01f, 9.99849409e-01f, 9.99764709e-01f, 9.99661192e-01f, 9.99538864e-01f,
    9.99397728e-01f, 9.99237790e-01f, 9.99059056e-01f, 9.98861533e-01f, 9.98645228e-01f, 9.98410150e-01f, 9.98156306e-01f, 9.97883707e-01f,
    9.97592363e-01f, 9.97282285e-01f, 9.96953485e-01f, 9.96605975e-01f, 9.96239767e-01f, 9.95854877e-01f, 9.95451318e-01f, 9.95029105e-01f,
    9.94588255e-01f, 9.94128784e-01f, 9.93650709e-01f, 9.93154049e-01f, 9.92638821e-01f, 9.92105046e-01f, 9.91552744e-01f, 9.90981935e-01f,
    9.90392640e-01f, 9.89784883e-01f, 9.89158685e-01f, 9.88514071e-01f, 9.87851065e-01f, 9.87169691e-01f, 9.86469976e-01f, 9.85751945e-01f,
    9.85015627e-01f, 9.84261047e-01f, 9.83488236e-01f, 9.82697221e-01f, 9.81888033e-01f, 9.81060702e-01f, 9.80215260e-01f, 9.79351737e-01f,
    9.78470168e-01f, 9.77570584e-01f, 9.76653020e-01f, 9.75717510e-01f, 9.74764090e-01f, 9.73792796e-01f, 9.72803663e-01f, 9.71796729e-01f,
    9.70772033e-01f, 9.69729612e-01f, 9.68669506e-01f, 9.67591755e-01f, 9.66496399e-01f, 9.65383481e-01f, 9.64253040e-01f, 9.63105121e-01f,
    9.61939766e-01f, 9.60757020e-01f, 9.59556926e-01f, 9.58339530e-01f, 9.57104878e-01f, 9.55853016e-01f, 9.54583992e-01f, 9.53297852e-01f,
    9.51994647e-01f, 9.50674424e-01f, 9.49337233e-01f, 9.47983125e-01f, 9.46612151e-01f, 9.45224362e-01f, 9.43819810e-01f, 9.42398549e-01f,
    9.40960632e-01f, 9.39506113e-01f, 9.38035047e-01f, 9.36547489e-01f, 9.35043496e-01f, 9.33523123e-01f, 9.31986428e-01f, 9.30433469e-01f,
    9.28864305e-01f, 9.27278994e-01f, 9.25677597e-01f, 9.24060172e-01f, 9.22426783e-01f, 9.20777489e-01f, 9.19112353e-01f, 9.17431437e-01f,
    9.15734806e-01f, 9.14022523e-01f, 9.12294651e-01f, 9.10551257e-01f, 9.08792407e-01f, 9.07018165e-01f, 9.05228599e-01f, 9.03423777e-01f,
    9.01603766e-01f, 8.99768635e-01f, 8.97918452e-01f, 8.96053289e-01f, 8.94173214e-01f, 8.92278299e-01f, 8.90368614e-01f, 8.88444233e-01f,
    8.86505227e-01f, 8.84551669e-01f, 8.82583633e-01f, 8.80601193e-01f, 8.78604423e-01f, 8.76593400e-01f, 8.74568197e-01f, 8.72528893e-01f,
    8.70475563e-01f, 8.68408284e-01f, 8.66327136e-01f, 8.64232195e-01f, 8.62123541e-01f, 8.60001254e-01f, 8.57865413e-01f, 8.55716098e-01f,
    8.53553391e-01f, 8.51377372e-01f, 8.49188125e-01f, 8.46985730e-01f, 8.44770272e-01f, 8.42541834e-01f, 8.40300499e-01f, 8.38046352e-01f,
    8.35779477e-01f, 8.33499961e-01f, 8.31207889e-01f, 8.28903347e-01f, 8.26586421e-01f, 8.24257201e-01f, 8.21915771e-01f, 8.19562222e-01f,
    8.17196642e-01f, 8.14819119e-01f, 8.12429744e-01f, 8.10028606e-01f, 8.07615795e-01f, 8.05191403e-01f, 8.02755521e-01f, 8.00308240e-01f,
    7.97849652e-01f, 7.95379851e-01f, 7.92898929e-01f, 7.90406979e-01f, 7.87904096e-01f, 7.85390373e-01f, 7.82865905e-01f, 7.80330788e-01f,
    7.77785117e-01f, 7.75228986e-01f, 7.72662494e-01f, 7.70085736e-01f, 7.67498810e-01f, 7.64901812e-01f, 7.62294841e-01f, 7.59677995e-01f,
    7.57051372e-01f, 7.54415071e-01f, 7.51769192e-01f, 7.49113833e-01f, 7.46449096e-01f, 7.43775080e-01f, 7.41091886e-01f, 7.38399615e-01f,
    7.35698368e-01f, 7.32988248e-01f, 7.30269355e-01f, 7.27541794e-01f, 7.24805665e-01f, 7.22061072e-01f, 7.19308119e-01f, 7.16546909e-01f,
    7.13777547e-01f, 7.11000135e-01f, 7.08214780e-01f, 7.05421586e-01f, 7.02620657e-01f, 6.99812100e-01f, 6.96996020e-01f, 6.94172523e-01f,
    6.91341716e-01f, 6.88503705e-01f, 6.85658597e-01f, 6.82806499e-01f, 6.79947518e-01f, 6.77081763e-01f, 6.74209340e-01f, 6.71330359e-01f,
    6.68444927e-01f, 6.65553153e-01f, 6.62655146e-01f, 6.59751015e-01f, 6.56840870e-01f, 6.53924820e-01f, 6.51002975e-01f, 6.48075444e-01f,
    6.45142339e-01f, 6.42203769e-01f, 6.39259845e-01f, 6.36310678e-01f, 6.33356379e-01f, 6.30397059e-01f, 6.27432830e-01f, 6.24463803e-01f,
    6.21490090e-01f, 6.18511803e-01f, 6.15529054e-01f, 6.12541956e-01f, 6.09550620e-01f, 6.06555160e-01f, 6.03555688e-01f, 6.00552317e-01f,
    5.97545161e-01f, 5.94534332e-01f, 5.91519944e-01f, 5.88502110e-01f, 5.85480944e-01f, 5.82456560e-01f, 5.79429072e-01f, 5.76398593e-01f,
    5.73365237e-01f, 5.70329120e-01f, 5.67290354e-01f, 5.64249055e-01f, 5.61205338e-01f, 5.58159315e-01f, 5.55111104e-01f, 5.52060817e-01f,
    5.49008570e-01f, 5.45954478e-01f, 5.42898656e-01f, 5.39841219e-01f, 5.36782282e-01f, 5.33721960e-01f, 5.30660368e-01f, 5.27597622e-01f,
    5.24533837e-01f, 5.21469128e-01f, 5.18403611e-01f, 5.15337402e-01f, 5.12270614e-01f, 5.09203365e-01f, 5.06135769e-01f, 5.03067942e-01f,
    5.00000000e-01f, 4.96932058e-01f, 4.93864231e-01f, 4.90796635e-01f, 4.87729386e-01f, 4.84662598e-01f, 4.81596389e-01f, 4.78530872e-01f,
    4.75466163e-01f, 4.72402378e-01f, 4.69339632e-01f, 4.66278040e-01f, 4.63217718e-01f, 4.60158781e-01f, 4.57101344e-01f, 4.54045522e-01f,
    4.50991430e-01f, 4.47939183e-01f, 4.44888896e-01f, 4.41840685e-01f, 4.38794662e-01f, 4.35750945e-01f, 4.32709646e-01f, 4.29670880e-01f,
    4.26634763e-01f, 4.23601407e-01f, 4.20570928e-01f, 4.17543440e-01f, 4.14519056e-01f, 4.11497890e-01f, 4.08480056e-01f, 4.05465668e-01f,
    4.02454839e-01f, 3.99447683e-01f, 3.96444312e-01f, 3.93444840e-01f, 3.90449380e-01f, 3.87458044e-01f, 3.84470946e-01f, 3.81488197e-01f,
    3.78509910e-01f, 3.75536197e-01f, 3.72567170e-01f, 3.69602941e-01f, 3.66643621e-01f, 3.63689322e-01f, 3.60740155e-01f, 3.57796231e-01f,
    3.54857661e-01f, 3.51924556e-01f, 3.48997025e-01f, 3.46075180e-01f, 3.43159130e-01f, 3.40248985e-01f, 3.37344854e-01f, 3.34446847e-01f,
    3.31555073e-01f, 3.28669641e-01f, 3.25790660e-01f, 3.22918237e-01f, 3.20052482e-01f, 3.17193501e-01f, 3.14341403e-01f, 3.11496295e-01f,
    3.08658284e-01f, 3.05827477e-01f, 3.03003980e-01f, 3.00187900e-01f, 2.97379343e-01f, 2.94578414e-01f, 2.91785220e-01f, 2.88999865e-01f,
    2.86222453e-01f, 2.83453091e-01f, 2.80691881e-01f, 2.77938928e-01f, 2.75194335e-01f, 2.72458206e-01f, 2.69730645e-01f, 2.67011752e-01f,
    2.64301632e-01f, 2.61600385e-01f, 2.58908114e-01f, 2.56224920e-01f, 2.53550904e-01f, 2.50886167e-01f, 2.48230808e-01f, 2.45584929e-01f,
    2.42948628e-01f, 2.40322005e-01f, 2.37705159e-01f, 2.35098188e-01f, 2.32501190e-01f, 2.29914264e-01f, 2.27337506e-01f, 2.24771014e-01f,
    2.22214883e-01f, 2.19669212e-01f, 2.17134095e-01f, 2.14609627e-01f, 2.12095904e-01f, 2.09593021e-01f, 2.07101071e-01f, 2.04620149e-01f,
    2.02150348e-01f, 1.99691760e-01f, 1.97244479e-01f, 1.94808597e-01f, 1.92384205e-01f, 1.89971394e-01f, 1.87570256e-01f, 1.85180881e-01f,
    1.82803358e-01f, 1.80437778e-01f, 1.78084229e-01f, 1.75742799e-01f, 1.73413579e-01f, 1.71096653e-01f, 1.68792111e-01f, 1.66500039e-01f,
    1.64220523e-01f, 1.61953648e-01f, 1.59699501e-01f, 1.57458166e-01f, 1.55229728e-01f, 1.53014270e-01f, 1.50811875e-01f, 1.48622628e-01f,
    1.46446609e-01f, 1.44283902e-01f, 1.42134587e-01f, 1.39998746e-01f, 1.37876459e-01f, 1.35767805e-01f, 1.33672864e-01f, 1.31591716e-01f,
    1.29524437e-01f, 1.27471107e-01f, 1.25431803e-01f, 1.23406600e-01f, 1.21395577e-01f, 1.19398807e-01f, 1.17416367e-01f, 1.15448331e-01f,
    1.13494773e-01f, 1.11555767e-01f, 1.09631386e-01f, 1.07721701e-01f, 1.05826786e-01f, 1.03946711e-01f, 1.02081548e-01f, 1.00231365e-01f,
    9.83962343e-02f, 9.65762232e-02f, 9.47714009e-02f, 9.29818351e-02f, 9.12075934e-02f, 8.94487425e-02f, 8.77053486e-02f, 8.59774774e-02f,
    8.42651938e-02f, 8.25685625e-02f, 8.08876472e-02f, 7.92225113e-02f, 7.75732174e-02f, 7.59398276e-02f, 7.43224034e-02f, 7.27210058e-02f,
    7.11356950e-02f, 6.95665307e-02f, 6.80135719e-02f, 6.64768772e-02f, 6.49565044e-02f, 6.34525108e-02f, 6.19649529e-02f, 6.04938868e-02f,
    5.90393678e-02f, 5.76014508e-02f, 5.61801898e-02f, 5.47756384e-02f, 5.33878494e-02f, 5.20168751e-02f, 5.06627672e-02f, 4.93255765e-02f,
    4.80053534e-02f, 4.67021477e-02f, 4.54160085e-02f, 4.41469840e-02f, 4.28951221e-02f, 4.16604700e-02f, 4.04430742e-02f, 3.92429803e-02f,
    3.80602337e-02f, 3.68948789e-02f, 3.57469598e-02f, 3.46165195e-02f, 3.35036006e-02f, 3.24082450e-02f, 3.13304940e-02f, 3.02703882e-02f,
    2.92279674e-02f, 2.82032709e-02f, 2.71963373e-02f, 2.62072045e-02f, 2.52359097e-02f, 2.42824895e-02f, 2.33469798e-02f, 2.24294158e-02f,
    2.15298321e-02f, 2.06482626e-02f, 1.97847403e-02f, 1.89392979e-02f, 1.81119671e-02f, 1.73027792e-02f, 1.65117645e-02f, 1.57389529e-02f,
    1.49843734e-02f, 1.42480545e-02f, 1.35300239e-02f, 1.28303086e-02f, 1.21489350e-02f, 1.14859287e-02f, 1.08413146e-02f, 1.02151172e-02f,
    9.60735980e-03f, 9.01806545e-03f, 8.44725628e-03f, 7.89495381e-03f, 7.36117881e-03f, 6.84595138e-03f, 6.34929092e-03f, 5.87121613e-03f,
    5.41174502e-03f, 4.97089487e-03f, 4.54868229e-03f, 4.14512317e-03f, 3.76023270e-03f, 3.39402538e-03f, 3.04651500e-03f, 2.71771463e-03f,
    2.40763666e-03f, 2.11629277e-03f, 1.84369391e-03f, 1.58985035e-03f, 1.35477166e-03f, 1.13846668e-03f, 9.40943550e-04f, 7.62209713e-04f,
    6.02271897e-04f, 4.61136124e-04f, 3.38807706e-04f, 2.35291249e-04f, 1.50590652e-04f, 8.47091021e-05f, 3.76490804e-05f, 9.41235870e-06f
};

 /* PDM Stuff*/
 const struct device * dmic_dev;
 const struct device * expander;
 struct pcm_stream_cfg stream;
 struct dmic_cfg cfg;
 
 /* LED Stuff*/
 #define INTENSITY 255
 #define NUMBER_OF_LEDS 3
 #define RED_LED DT_GPIO_PIN(DT_NODELABEL(led0), gpios)
 #define GREEN_LED DT_GPIO_PIN(DT_NODELABEL(led1), gpios)
 #define BLUE_LED DT_GPIO_PIN(DT_NODELABEL(led2), gpios)
 
 enum sx1509b_color { sx1509b_red = 0, sx1509b_green, sx1509b_blue };
 const struct device *sx1509b_dev;
 static const gpio_pin_t rgb_pins[] = {
     RED_LED,
     GREEN_LED,
     BLUE_LED,
 };
 
 
 /* Ring Buffer Stuff*/
 #define ONE_BLOCK_SIZE BLOCK_SIZE(MAX_SAMPLE_RATE, 1)
 #define BUFFERED_BLOCKS BLOCK_COUNT
 #define RING_BUF_SIZE_BYTES ONE_BLOCK_SIZE * BUFFERED_BLOCKS
 RING_BUF_DECLARE(pcm_ring, RING_BUF_SIZE_BYTES);
 
 #define PDM_STACK_SIZE 512
 #define PDM_PRIORITY 5
 
 #define PROC_STACK_SIZE 2048
 #define PROC_PRIORITY 7
 
 K_THREAD_STACK_DEFINE(pdm_stack, PDM_STACK_SIZE);
 static struct k_thread pdm_thread_data;
 
 K_THREAD_STACK_DEFINE(proc_stack, PROC_STACK_SIZE);
 static struct k_thread proc_thread_data;
 
 void fft_real32(int32_t *in, int32_t *out, int length);
 K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, SLAB_DEPTH, 4);

 static char result[5];

 static const struct {
     char name;
     uint8_t base_oct;
     float32_t base_freq;
 } strings[6] = {
     { 'E', 2,  82.407f }, /* low-E - 82.41 Hz */
     { 'A', 2, 110.000f }, /* A - 110.00 Hz */
     { 'D', 3, 146.832f }, /* D - 146.83 Hz */
     { 'G', 3, 196.000f }, /* G - 196.00 Hz */
     { 'B', 3, 246.942f}, /* B - 246.94 Hz */
     { 'E', 4, 329.628f} /* high-E - 329.63 Hz */
 };

 static void led_set_colour(int intensity_red, int intensity_green, int intensity_blue){
    sx1509b_led_intensity_pin_set(sx1509b_dev, RED_LED, intensity_red);
    sx1509b_led_intensity_pin_set(sx1509b_dev, GREEN_LED,intensity_green);
    sx1509b_led_intensity_pin_set(sx1509b_dev, BLUE_LED, intensity_blue);

}

 void status_led(bool isValid) {
    if (isValid) {
        led_set_colour(0, 255, 0);
    } else {
        led_set_colour(255, 0, 0);
    }
}
 
 static const char* frequencyToNote(float32_t f) {
     if (f < 70.f || f > 4000.f) {
         return "—";
     }
 
     float32_t best_err = 1e9f;
     int       best_i   = -1;
     int8_t    best_oct = 0;
 
     for (int i = 0; i < 6; ++i) {
         /* Find the nearest octave of this open string to the measured f  */
         float32_t ratio = f / strings[i].base_freq;
         int8_t    k     = (int8_t)roundf(log2f(ratio));
         float32_t cand  = strings[i].base_freq * powf(2.f, k);
         float32_t err   = fabsf(f - cand);
 
         if (err < best_err) {
             best_err = err;
             best_i   = i;
             best_oct = strings[i].base_oct + k;
         }
     }
 
     if (best_i < 0 || best_err > 0.06f * f)
         return "—";
 
     /* Format such that its in form “E2”, “G3”*/
     snprintf(result, sizeof result, "%c%d", strings[best_i].name, best_oct);
     return result;
 }
 
 
 int init_led(void){
     int err;
     sx1509b_dev = DEVICE_DT_GET(DT_NODELABEL(sx1509b));
     if (!device_is_ready(sx1509b_dev)) {
         LOG_DBG("sx1509b: device not ready.\n");
         return -ENODEV;
     }
     for (int i = 0; i < NUMBER_OF_LEDS; i++) {
         err = sx1509b_led_intensity_pin_configure(sx1509b_dev, rgb_pins[i]);
         if (err) {
             LOG_DBG("Error configuring pin for LED intensity\n");
             return -ENODEV;
         }
     }
     return 0;
 }
 
 
 static void pdm_thread_entry(void *p1, void *p2, void *p3){
     void *buffer;
     uint32_t size;
     uint8_t *write;
     size_t written;
 
     dmic_configure(dmic_dev, &cfg);
     dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
     int ret;
     while (1){
         ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
         if (ret < 0 || buffer == NULL){
             if (buffer){
                 k_mem_slab_free(&mem_slab, buffer);
             }
             k_msleep(READ_DELAY_MS);
             continue;
         }
 
         written = ring_buf_put_claim(&pcm_ring, (uint8_t **)&write, ONE_BLOCK_SIZE);
         if (written > 0) {
             memcpy(write, buffer, written);
             ring_buf_put_finish(&pcm_ring, written);
         }
         k_mem_slab_free(&mem_slab, buffer);
         k_msleep(READ_DELAY_MS);
     }
 }
 
 static void proc_thread_entry(void *p1, void *p2, void *p3)
 {
     int16_t  *pcm_buf;
     size_t    got;
 
     while (1) {
         do {
             got = ring_buf_get_claim(&pcm_ring,
                                      (uint8_t **)&pcm_buf,
                                      ONE_BLOCK_SIZE);
             if (got < ONE_BLOCK_SIZE) {
                 k_msleep(1);
             }
         } while (got < ONE_BLOCK_SIZE);

         size_t sample_count = got / BYTES_PER_SAMPLE;

         /* --- FFT on raw PCM --- */
         for (uint16_t n = 0; n < FFT_LEN; n++) {
            float32_t s = (n < sample_count) ? (float32_t)pcm_buf[n] : 0.0f;
            mono_f32[n] = s * hann[n];
        }

        for (uint16_t n = 0; n < FFT_LEN; n++) {
            cbuf[2*n] = mono_f32[n];
            cbuf[2*n + 1] = 0.0f;
        }

        arm_cfft_f32(&arm_cfft_sR_f32_len1024, cbuf, 0, 1);
        arm_cmplx_mag_f32(cbuf, mag, FFT_LEN);

        uint16_t max_idx = 0;  float32_t max_val = mag[0];
        for (uint16_t i = 1; i < FFT_LEN; ++i)
            if (mag[i] > max_val) { max_val = mag[i]; max_idx = i; }
        
        if (max_idx > FFT_LEN/2) {
            max_idx = 0; max_val = mag[0];
            for (uint16_t i = 1; i < FFT_LEN/2; ++i)
                if (mag[i] > max_val) { max_val = mag[i]; max_idx = i; }
        }
        // Parabolic Interpolation
        float32_t delta = 0.0f;
        if (max_idx > 0 && max_idx < FFT_LEN/2 - 1) {
            float32_t alpha = mag[max_idx - 1];
            float32_t beta  = mag[max_idx];
            float32_t gamma = mag[max_idx + 1];
            float32_t denom = (alpha - 2.0f*beta + gamma);
            if (denom != 0.0f){
                delta = 0.5f * (alpha - gamma) / denom;
            }
        }
        float32_t refined_bin = (float32_t)max_idx + delta;
        float32_t freq = refined_bin * ((float32_t)cfg.streams[0].pcm_rate /
                                        (float32_t)FFT_LEN);
         printk("FFT peak: %.1f Hz\n", (double)freq);
         const char *detected = frequencyToNote(freq);
         printk("Peak %.1f Hz and Note: %s\n", (double)freq, detected);
        if (current_mode == MODE_TUNE) {
            if (strcmp(detected, target_note) == 0) {
                led_set_colour(0, 255, 0);
            } else {
                led_set_colour(255, 0, 0);
            }
        } else {
            printk("Detected note: %s\n", detected);
            led_set_colour(0, 0, 255);
        }
         ring_buf_get_finish(&pcm_ring, got);
     }
 }
 
  int init_microphone(void){
     // Enable Microphone 
     dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
 
     LOG_INF("Sound Sampler Thingy52");
 
     if (!device_is_ready(dmic_dev)) {
         LOG_ERR("%s is not ready", dmic_dev->name);
         return 0;
     }
     expander = DEVICE_DT_GET(DT_NODELABEL(sx1509b));
    if (!device_is_ready(expander)) {
        LOG_ERR("%s is not ready", expander->name);
        return 0;
    }
    gpio_pin_configure(expander, 9, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(expander, 9, 1);
 
    stream = (struct pcm_stream_cfg){
     .pcm_width = SAMPLE_BIT_WIDTH,
     .mem_slab  = &mem_slab,
     };
     cfg = (struct dmic_cfg){
         .io = {
             /* These fields can be used to limit the PDM clock
             * configurations that the driver is allowed to use
             * to those supported by the microphone.
             */
             .min_pdm_clk_freq = 1000000,
             .max_pdm_clk_freq = 3500000,
             .min_pdm_clk_dc   = 40,
             .max_pdm_clk_dc   = 60,
         },
         .streams = &stream,
         .channel = {
             .req_num_streams = 1,
         },
     };
 
     cfg.channel.req_num_chan = 1;
     cfg.channel.req_chan_map_lo =
         dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
     cfg.streams[0].pcm_rate = MAX_SAMPLE_RATE;
     cfg.streams[0].block_size =
         BLOCK_SIZE(cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);
 
     return 0;
  }
  
  int main(void) {
     init_microphone();
     init_led();
     init_bluetooth();
     led_set_colour(0, 0, 255);

     k_thread_create(&pdm_thread_data, pdm_stack, 
         sizeof(pdm_stack), pdm_thread_entry, 
         NULL, NULL, NULL, PDM_PRIORITY, 0, K_NO_WAIT);
 
     k_thread_name_set(&pdm_thread_data, "pdm_prod");
 
     k_thread_create(&proc_thread_data, proc_stack, sizeof(proc_stack),
                     proc_thread_entry, NULL, NULL, NULL,
                     PROC_PRIORITY, 0, K_NO_WAIT);
                     
     k_thread_name_set(&proc_thread_data, "pdm_cons");
     
     LOG_INF("Exiting");
     return 0;
  }
  
 