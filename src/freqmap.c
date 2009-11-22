/* =========================================================================
 * File:        FREQMAP.C
 * Description: This module is responsible for mapping between
 *              frequency and frequency table with there named channels.
 *              Please note that named channels have nothng to do with the
 *              broadcasting names. The channels names are standardized names
 *              for frequences. The names changes depending on geographic
 *              location and hence each geographic location uses its own
 *              frequency map. By defaylt the "west-euprope" map will be used.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id: freqmap.c 138 2009-11-19 11:34:29Z ljp $
 *
 * Copyright (C) 2009 Johan Persson
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 * =========================================================================
 */

// We want the full POSIX and C99 standard
#define _GNU_SOURCE

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <iniparser.h>
#include <errno.h>

#include "tvpvrd.h"
#include "freqmap.h"
#include "utils.h"

/*
 * Structure to hold all frequency channel maps. Each maps consists of
 * (table name, number of recors, pointer to actual frequency map)
 * This structure gets the pointer to the actual maps resolved
 * at runtime initialization.
 *
 * Each acual frequency map consists of a list of pairs of
 * (Frequency,name) tuples
 *
 * NOTE: The order of this table MUST correspond to the enums in freqm_t
 */
static struct freqmap frequence_map[] = {
    { "europe-west", 0, NULL},
    { "europe-east", 0, NULL},
    { "france", 0, NULL},
    { "ireland", 0, NULL},
    { "italy", 0, NULL},
    { "australia", 0, NULL},
    { "newzealand", 0, NULL},
    { "us-bcast", 0, NULL},
    { "us-cable", 0, NULL}

    /* The following mappings are not yet implemented
    { "us-cable-hrc", 0, NULL},
    { "us-cable-irc", 0, NULL},
    { "japan-bcast", 0, NULL},
    { "japan-cable", 0, NULL},
    { "china-bcast", 0, NULL},
    { "southafrica", 0, NULL},
    { "argentina", 0, NULL},
    { "australia-optus", 0, NULL}
     */
};

/*
 * Determine size of table of all frequency maps
 */
static const unsigned int num_fmap = sizeof (frequence_map) / sizeof (struct freqmap);

/*
 * Keep a global variable of the map currently in use
 */
static freqm_t curr_fmap=FREQMAP_EUROPEWEST;

/*
 * West Europe standard frequencies.
 * Channel width 7MHz or 8MHz
 */
static struct freqch europe_west_chtable[] = {
    {48250, "E2"},
    {55250, "E3"},
    {62250, "E4"},
    {69250, "S01"},
    {76250, "S02"},
    {83250, "S03"},
    {175250, "E5"},
    {182250, "E6"},
    {189250, "E7"},
    {196250, "E8"},
    {203250, "E9"},
    {210250, "E10"},
    {217250, "E11"},
    {224250, "E12"},
    {105250, "SE1"},
    {112250, "SE2"},
    {119250, "SE3"},
    {126250, "SE4"},
    {133250, "SE5"},
    {140250, "SE6"},
    {147250, "SE7"},
    {154250, "SE8"},
    {161250, "SE9"},
    {168250, "SE10"},
    {231250, "SE11"},
    {238250, "SE12"},
    {245250, "SE13"},
    {252250, "SE14"},
    {259250, "SE15"},
    {266250, "SE16"},
    {273250, "SE17"},
    {280250, "SE18"},
    {287250, "SE19"},
    {294250, "SE20"},
    {303250, "S21"},
    {311250, "S22"},
    {319250, "S23"},
    {327250, "S24"},
    {335250, "S25"},
    {343250, "S26"},
    {351250, "S27"},
    {359250, "S28"},
    {367250, "S29"},
    {375250, "S30"},
    {383250, "S31"},
    {391250, "S32"},
    {399250, "S33"},
    {407250, "S34"},
    {415250, "S35"},
    {423250, "S36"},
    {431250, "S37"},
    {439250, "S38"},
    {447250, "S39"},
    {455250, "S40"},
    {463250, "S41"},
    {471250, "21"},
    {479250, "22"},
    {487250, "23"},
    {495250, "24"},
    {503250, "25"},
    {511250, "26"},
    {519250, "27"},
    {527250, "28"},
    {535250, "29"},
    {543250, "30"},
    {551250, "31"},
    {559250, "32"},
    {567250, "33"},
    {575250, "34"},
    {583250, "35"},
    {591250, "36"},
    {599250, "37"},
    {607250, "38"},
    {615250, "39"},
    {623250, "40"},
    {631250, "41"},
    {639250, "42"},
    {647250, "43"},
    {655250, "44"},
    {663250, "45"},
    {671250, "46"},
    {679250, "47"},
    {687250, "48"},
    {695250, "49"},
    {703250, "50"},
    {711250, "51"},
    {719250, "52"},
    {727250, "53"},
    {735250, "54"},
    {743250, "55"},
    {751250, "56"},
    {759250, "57"},
    {767250, "58"},
    {775250, "59"},
    {783250, "60"},
    {791250, "61"},
    {799250, "62"},
    {807250, "63"},
    {815250, "64"},
    {823250, "65"},
    {831250, "66"},
    {839250, "67"},
    {847250, "68"},
    {855250, "69"}
};

static struct freqch france_chtable[] = {
    {47750, "K01"},
    {55750, "K02"},
    {60500, "K03"},
    {63750, "K04"},
    {176000, "K05"},
    {184000, "K06"},
    {192000, "K07"},
    {200000, "K08"},
    {208000, "K09"},
    {216000, "K10"},
    {116750, "KB"},
    {128750, "KC"},
    {140750, "KD"},
    {159750, "KE"},
    {164750, "KF"},
    {176750, "KG"},
    {188750, "KH"},
    {200750, "KI"},
    {212750, "KJ"},
    {224750, "KK"},
    {236750, "KL"},
    {248750, "KM"},
    {260750, "KN"},
    {272750, "KO"},
    {284750, "KP"},
    {296750, "KQ"},
    {303250, "H01"},
    {311250, "H02"},
    {319250, "H03"},
    {327250, "H04"},
    {335250, "H05"},
    {343250, "H06"},
    {351250, "H07"},
    {359250, "H08"},
    {367250, "H09"},
    {375250, "H10"},
    {383250, "H11"},
    {391250, "H12"},
    {399250, "H13"},
    {407250, "H14"},
    {415250, "H15"},
    {423250, "H16"},
    {431250, "H17"},
    {439250, "H18"},
    {447250, "H19"},
    {471250, "21"},
    {479250, "22"},
    {487250, "23"},
    {495250, "24"},
    {503250, "25"},
    {511250, "26"},
    {519250, "27"},
    {527250, "28"},
    {535250, "29"},
    {543250, "30"},
    {551250, "31"},
    {559250, "32"},
    {567250, "33"},
    {575250, "34"},
    {583250, "35"},
    {591250, "36"},
    {599250, "37"},
    {607250, "38"},
    {615250, "39"},
    {623250, "40"},
    {631250, "41"},
    {639250, "42"},
    {647250, "43"},
    {655250, "44"},
    {663250, "45"},
    {671250, "46"},
    {679250, "47"},
    {687250, "48"},
    {695250, "49"},
    {703250, "50"},
    {711250, "51"},
    {719250, "52"},
    {727250, "53"},
    {735250, "54"},
    {743250, "55"},
    {751250, "56"},
    {759250, "57"},
    {767250, "58"},
    {775250, "59"},
    {783250, "60"},
    {791250, "61"},
    {799250, "62"},
    {807250, "63"},
    {815250, "64"},
    {823250, "65"},
    {831250, "66"},
    {839250, "67"},
    {847250, "68"},
    {855250, "69"}
};

static struct freqch europe_east_chtable[] = {
{49750,"R1"},
{59250,"R2"},
{77250,"R3"},
{85250,"R4"},
{93250,"R5"},
{175250,"R6"},
{183250,"R7"},
{191250,"R8"},
{199250,"R9"},
{207250,"R10"},
{215250,"R11"},
{223250,"R12"},
{111250,"SR1"},
{119250,"SR2"},
{127250,"SR3"},
{135250,"SR4"},
{143250,"SR5"},
{151250,"SR6"},
{159250,"SR7"},
{167250,"SR8"},
{231250,"SR11"},
{239250,"SR12"},
{247250,"SR13"},
{255250,"SR14"},
{263250,"SR15"},
{271250,"SR16"},
{279250,"SR17"},
{287250,"SR18"},
{295250,"SR19"},
{48250,"E2"},
{55250,"E3"},
{62250,"E4"},
{69250,"S01"},
{76250,"S02"},
{83250,"S03"},
{175250,"E5"},
{182250,"E6"},
{189250,"E7"},
{196250,"E8"},
{203250,"E9"},
{210250,"E10"},
{217250,"E11"},
{224250,"E12"},
{105250,"SE1"},
{112250,"SE2"},
{119250,"SE3"},
{126250,"SE4"},
{133250,"SE5"},
{140250,"SE6"},
{147250,"SE7"},
{154250,"SE8"},
{161250,"SE9"},
{168250,"SE10"},
{231250,"SE11"},
{238250,"SE12"},
{245250,"SE13"},
{252250,"SE14"},
{259250,"SE15"},
{266250,"SE16"},
{273250,"SE17"},
{280250,"SE18"},
{287250,"SE19"},
{294250,"SE20"},
{303250,"S21"},
{311250,"S22"},
{319250,"S23"},
{327250,"S24"},
{335250,"S25"},
{343250,"S26"},
{351250,"S27"},
{359250,"S28"},
{367250,"S29"},
{375250,"S30"},
{383250,"S31"},
{391250,"S32"},
{399250,"S33"},
{407250,"S34"},
{415250,"S35"},
{423250,"S36"},
{431250,"S37"},
{439250,"S38"},
{447250,"S39"},
{455250,"S40"},
{463250,"S41"},
{471250,"21"},
{479250,"22"},
{487250,"23"},
{495250,"24"},
{503250,"25"},
{511250,"26"},
{519250,"27"},
{527250,"28"},
{535250,"29"},
{543250,"30"},
{551250,"31"},
{559250,"32"},
{567250,"33"},
{575250,"34"},
{583250,"35"},
{591250,"36"},
{599250,"37"},
{607250,"38"},
{615250,"39"},
{623250,"40"},
{631250,"41"},
{639250,"42"},
{647250,"43"},
{655250,"44"},
{663250,"45"},
{671250,"46"},
{679250,"47"},
{687250,"48"},
{695250,"49"},
{703250,"50"},
{711250,"51"},
{719250,"52"},
{727250,"53"},
{735250,"54"},
{743250,"55"},
{751250,"56"},
{759250,"57"},
{767250,"58"},
{775250,"59"},
{783250,"60"},
{791250,"61"},
{799250,"62"},
{807250,"63"},
{815250,"64"},
{823250,"65"},
{831250,"66"},
{839250,"67"},
{847250,"68"},
{855250,"69"}
};

static struct freqch ireland_chtable[] = {
    {45750, "A0"},
    {48000, "A1"},
    {53750, "A2"},
    {56000, "A3"},
    {61750, "A4"},
    {64000, "A5"},
    {175250, "A6"},
    {176000, "A7"},
    {183250, "A8"},
    {184000, "A9"},
    {191250, "A10"},
    {192000, "A11"},
    {199250, "A12"},
    {200000, "A13"},
    {207250, "A14"},
    {208000, "A15"},
    {215250, "A16"},
    {216000, "A17"},
    {224000, "A18"},
    {232000, "A19"},
    {248000, "A20"},
    {256000, "A21"},
    {264000, "A22"},
    {272000, "A23"},
    {280000, "A24"},
    {288000, "A25"},
    {296000, "A26"},
    {304000, "A27"},
    {312000, "A28"},
    {320000, "A29"},
    {344000, "A30"},
    {352000, "A31"},
    {408000, "A32"},
    {416000, "A33"},
    {448000, "A34"},
    {480000, "A35"},
    {520000, "A36"},
    {471250, "21"},
    {479250, "22"},
    {487250, "23"},
    {495250, "24"},
    {503250, "25"},
    {511250, "26"},
    {519250, "27"},
    {527250, "28"},
    {535250, "29"},
    {543250, "30"},
    {551250, "31"},
    {559250, "32"},
    {567250, "33"},
    {575250, "34"},
    {583250, "35"},
    {591250, "36"},
    {599250, "37"},
    {607250, "38"},
    {615250, "39"},
    {623250, "40"},
    {631250, "41"},
    {639250, "42"},
    {647250, "43"},
    {655250, "44"},
    {663250, "45"},
    {671250, "46"},
    {679250, "47"},
    {687250, "48"},
    {695250, "49"},
    {703250, "50"},
    {711250, "51"},
    {719250, "52"},
    {727250, "53"},
    {735250, "54"},
    {743250, "55"},
    {751250, "56"},
    {759250, "57"},
    {767250, "58"},
    {775250, "59"},
    {783250, "60"},
    {791250, "61"},
    {799250, "62"},
    {807250, "63"},
    {815250, "64"},
    {823250, "65"},
    {831250, "66"},
    {839250, "67"},
    {847250, "68"},
    {855250, "69"}
};

static struct freqch italy_chtable[] = {
    {53750, "A"},
    {62250, "B"},
    {82250, "C"},
    {175250, "D"},
    {183750, "E"},
    {192250, "F"},
    {201250, "G"},
    {210250, "H"},
    {217250, "H1"},
    {224250, "H2"},
    {471250, "21"},
    {479250, "22"},
    {487250, "23"},
    {495250, "24"},
    {503250, "25"},
    {511250, "26"},
    {519250, "27"},
    {527250, "28"},
    {535250, "29"},
    {543250, "30"},
    {551250, "31"},
    {559250, "32"},
    {567250, "33"},
    {575250, "34"},
    {583250, "35"},
    {591250, "36"},
    {599250, "37"},
    {607250, "38"},
    {615250, "39"},
    {623250, "40"},
    {631250, "41"},
    {639250, "42"},
    {647250, "43"},
    {655250, "44"},
    {663250, "45"},
    {671250, "46"},
    {679250, "47"},
    {687250, "48"},
    {695250, "49"},
    {703250, "50"},
    {711250, "51"},
    {719250, "52"},
    {727250, "53"},
    {735250, "54"},
    {743250, "55"},
    {751250, "56"},
    {759250, "57"},
    {767250, "58"},
    {775250, "59"},
    {783250, "60"},
    {791250, "61"},
    {799250, "62"},
    {807250, "63"},
    {815250, "64"},
    {823250, "65"},
    {831250, "66"},
    {839250, "67"},
    {847250, "68"},
    {855250, "69"},

};

static struct freqch australia_chtable[] = {
    {46250, "0"},
    {57250, "1"},
    {64250, "2"},
    {86250, "3"},
    {95250, "4"},
    {102250, "5"},
    {138250, "5A"},
    {175250, "6"},
    {182250, "7"},
    {189250, "8"},
    {196250, "9"},
    {209250, "10"},
    {216250, "11"},
    {527250, "28"},
    {534250, "29"},
    {541250, "30"},
    {548250, "31"},
    {555250, "32"},
    {562250, "33"},
    {569250, "34"},
    {576250, "35"},
    {591250, "36"},
    {604250, "39"},
    {611250, "40"},
    {618250, "41"},
    {625250, "42"},
    {632250, "43"},
    {639250, "44"},
    {646250, "45"},
    {653250, "46"},
    {660250, "47"},
    {667250, "48"},
    {674250, "49"},
    {681250, "50"},
    {688250, "51"},
    {695250, "52"},
    {702250, "53"},
    {709250, "54"},
    {716250, "55"},
    {723250, "56"},
    {730250, "57"},
    {737250, "58"},
    {744250, "59"},
    {751250, "60"},
    {758250, "61"},
    {765250, "62"},
    {772250, "63"},
    {779250, "64"},
    {786250, "65"},
    {793250, "66"},
    {800250, "67"},
    {807250, "68"},
    {814250, "69"},

};

static struct freqch newzealand_chtable[] = {
    {45250, "1"},
    {55250, "2"},
    {62250, "3"},
    {175250, "4"},
    {182250, "5"},
    {189250, "6"},
    {196250, "7"},
    {203250, "8"},
    {210250, "9"},
    {217250, "10"},
    {224250, "11"},
    {471250, "21"},
    {479250, "22"},
    {487250, "23"},
    {495250, "24"},
    {503250, "25"},
    {511250, "26"},
    {519250, "27"},
    {527250, "28"},
    {535250, "29"},
    {543250, "30"},
    {551250, "31"},
    {559250, "32"},
    {567250, "33"},
    {575250, "34"},
    {583250, "35"},
    {591250, "36"},
    {599250, "37"},
    {607250, "38"},
    {615250, "39"},
    {623250, "40"},
    {631250, "41"},
    {639250, "42"},
    {647250, "43"},
    {655250, "44"},
    {663250, "45"},
    {671250, "46"},
    {679250, "47"},
    {687250, "48"},
    {695250, "49"},
    {703250, "50"},
    {711250, "51"},
    {719250, "52"},
    {727250, "53"},
    {735250, "54"},
    {743250, "55"},
    {751250, "56"},
    {759250, "57"},
    {767250, "58"},
    {775250, "59"},
    {783250, "60"},
    {791250, "61"},
    {799250, "62"},
    {807250, "63"},
    {815250, "64"},
    {823250, "65"},
    {831250, "66"},
    {839250, "67"},
    {847250, "68"},
    {855250, "69"},
};

static struct freqch usbcast_chtable[] = {
    {55250, "2"},
    {61250, "3"},
    {67250, "4"},
    {77250, "5"},
    {83250, "6"},
    {175250, "7"},
    {181250, "8"},
    {187250, "9"},
    {193250, "10"},
    {199250, "11"},
    {205250, "12"},
    {211250, "13"},
    {471250, "14"},
    {477250, "15"},
    {483250, "16"},
    {489250, "17"},
    {495250, "18"},
    {501250, "19"},
    {507250, "20"},
    {513250, "21"},
    {519250, "22"},
    {525250, "23"},
    {531250, "24"},
    {537250, "25"},
    {543250, "26"},
    {549250, "27"},
    {555250, "28"},
    {561250, "29"},
    {567250, "30"},
    {573250, "31"},
    {579250, "32"},
    {585250, "33"},
    {591250, "34"},
    {597250, "35"},
    {603250, "36"},
    {609250, "37"},
    {615250, "38"},
    {621250, "39"},
    {627250, "40"},
    {633250, "41"},
    {639250, "42"},
    {645250, "43"},
    {651250, "44"},
    {657250, "45"},
    {663250, "46"},
    {669250, "47"},
    {675250, "48"},
    {681250, "49"},
    {687250, "50"},
    {693250, "51"},
    {699250, "52"},
    {705250, "53"},
    {711250, "54"},
    {717250, "55"},
    {723250, "56"},
    {729250, "57"},
    {735250, "58"},
    {741250, "59"},
    {747250, "60"},
    {753250, "61"},
    {759250, "62"},
    {765250, "63"},
    {771250, "64"},
    {777250, "65"},
    {783250, "66"},
    {789250, "67"},
    {795250, "68"},
    {801250, "69"},
    {807250, "70"},
    {813250, "71"},
    {819250, "72"},
    {825250, "73"},
    {831250, "74"},
    {837250, "75"},
    {843250, "76"},
    {849250, "77"},
    {855250, "78"},
    {861250, "79"},
    {867250, "80"},
    {873250, "81"},
    {879250, "82"},
    {885250, "83"},

};

static struct freqch uscable_chtable[] = {
    {73250, "1"},
    {55250, "2"},
    {61250, "3"},
    {67250, "4"},
    {77250, "5"},
    {83250, "6"},
    {175250, "7"},
    {181250, "8"},
    {187250, "9"},
    {193250, "10"},
    {199250, "11"},
    {205250, "12"},
    {211250, "13"},
    {121250, "14"},
    {127250, "15"},
    {133250, "16"},
    {139250, "17"},
    {145250, "18"},
    {151250, "19"},
    {157250, "20"},
    {163250, "21"},
    {169250, "22"},
    {217250, "23"},
    {223250, "24"},
    {229250, "25"},
    {235250, "26"},
    {241250, "27"},
    {247250, "28"},
    {253250, "29"},
    {259250, "30"},
    {265250, "31"},
    {271250, "32"},
    {277250, "33"},
    {283250, "34"},
    {289250, "35"},
    {295250, "36"},
    {301250, "37"},
    {307250, "38"},
    {313250, "39"},
    {319250, "40"},
    {325250, "41"},
    {331250, "42"},
    {337250, "43"},
    {343250, "44"},
    {349250, "45"},
    {355250, "46"},
    {361250, "47"},
    {367250, "48"},
    {373250, "49"},
    {379250, "50"},
    {385250, "51"},
    {391250, "52"},
    {397250, "53"},
    {403250, "54"},
    {409250, "55"},
    {415250, "56"},
    {421250, "57"},
    {427250, "58"},
    {433250, "59"},
    {439250, "60"},
    {445250, "61"},
    {451250, "62"},
    {457250, "63"},
    {463250, "64"},
    {469250, "65"},
    {475250, "66"},
    {481250, "67"},
    {487250, "68"},
    {493250, "69"},
    {499250, "70"},
    {505250, "71"},
    {511250, "72"},
    {517250, "73"},
    {523250, "74"},
    {529250, "75"},
    {535250, "76"},
    {541250, "77"},
    {547250, "78"},
    {553250, "79"},
    {559250, "80"},
    {565250, "81"},
    {571250, "82"},
    {577250, "83"},
    {583250, "84"},
    {589250, "85"},
    {595250, "86"},
    {601250, "87"},
    {607250, "88"},
    {613250, "89"},
    {619250, "90"},
    {625250, "91"},
    {631250, "92"},
    {637250, "93"},
    {643250, "94"},
    {91250, "95"},
    {97250, "96"},
    {103250, "97"},
    {109250, "98"},
    {115250, "99"},
    {649250, "100"},
    {655250, "101"},
    {661250, "102"},
    {667250, "103"},
    {673250, "104"},
    {679250, "105"},
    {685250, "106"},
    {691250, "107"},
    {697250, "108"},
    {703250, "109"},
    {709250, "110"},
    {715250, "111"},
    {721250, "112"},
    {727250, "113"},
    {733250, "114"},
    {739250, "115"},
    {745250, "116"},
    {751250, "117"},
    {757250, "118"},
    {763250, "119"},
    {769250, "120"},
    {775250, "121"},
    {781250, "122"},
    {787250, "123"},
    {793250, "124"},
    {799250, "125"},
    {8250, "T7"},
    {14250, "T8"},
    {20250, "T9"},
    {26250, "T10"},
    {32250, "T11"},
    {38250, "T12"},
    {44250, "T13"},
    {50250, "T14"},

};


/**
 * Initialize the frequency table. Must be called in the beginning of the
 * program
 */
void
initfreqtable(void) {
    frequence_map[FREQMAP_EUROPEWEST].tbl = europe_west_chtable;
    frequence_map[FREQMAP_EUROPEWEST].size = sizeof (europe_west_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_EUROPEEAST].tbl = europe_east_chtable;
    frequence_map[FREQMAP_EUROPEEAST].size = sizeof (europe_east_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_FRANCE].tbl = france_chtable;
    frequence_map[FREQMAP_FRANCE].size = sizeof (france_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_IRELAND].tbl = ireland_chtable;
    frequence_map[FREQMAP_IRELAND].size = sizeof (ireland_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_ITALY].tbl = italy_chtable;
    frequence_map[FREQMAP_ITALY].size = sizeof (italy_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_AUSTRALIA].tbl = australia_chtable;
    frequence_map[FREQMAP_AUSTRALIA].size = sizeof (australia_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_NEWZEALAND].tbl = newzealand_chtable;
    frequence_map[FREQMAP_NEWZEALAND].size = sizeof (newzealand_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_USBCAST].tbl = usbcast_chtable;
    frequence_map[FREQMAP_USBCAST].size = sizeof (usbcast_chtable) / sizeof (struct freqch);

    frequence_map[FREQMAP_USCABLE].tbl = uscable_chtable;
    frequence_map[FREQMAP_USCABLE].size = sizeof (uscable_chtable) / sizeof (struct freqch);
}

struct station_map_entry {
    char name[32];
    char channel[32];
};

static struct station_map_entry station_map[256] ;
static int num_stations = 0 ;

/**
 * Try to read broadcast names mapping to channel names
 * @param name
 * @return -1 on failure, 0 on success
 */
int
read_xawtvfile(const char *name) {
    dictionary *dict;

    dict = iniparser_load(name);
    if( dict == NULL ) {
        logmsg(LOG_ERR,"Could not read xawtv channel file \"%s\" (%d ; %s). ",name,errno,strerror(errno));
        return -1;
    }

    // Loop through all sections to find the corresponding channel
    int nsec = iniparser_getnsec(dict);
    char buffer[64];
    for(int i=0; i < nsec; i++) {
        char *sname = iniparser_getsecname(dict, i);
        strncpy(buffer,sname,64-10);
        strncat(buffer,":channel",63);
        buffer[63] = '\0';
        char *station_name = iniparser_getstring(dict,buffer,NULL);
        if( station_name ) {
            strncpy(station_map[num_stations].name,sname,31);
            station_map[num_stations].name[31] = '\0';
            strncpy(station_map[num_stations].channel,station_name,31);
            station_map[num_stations].channel[31] = '\0';
            num_stations++;
        }
    }
    logmsg(LOG_NOTICE,"Read xawtv channel file \"%s\". Found %d stations.",name,num_stations);
    return 0;
}

void
list_stations(int fd) {
    for(int i=0; i<num_stations; i++) {
        _writef(fd,"%6s: %s\n",station_map[i].channel,station_map[i].name);
    }
}

/**
 * Translate a station name to a channel name
 * @param station
 * @param chbuffer
 * @param size
 * @return 0 on success, -1 on failure
 */
int
get_chfromstation(const char *station, char *chbuffer, int size) {
    for(int i=0; i<num_stations; i++) {
        if( stricmp(station,station_map[i].name) == 0  ) {
            strncpy(chbuffer, station_map[i].channel, size);
            chbuffer[size-1]='\0';
            return 0;
        }
    }
    return -1;
}

/**
 * Set the current frequency map
 * @param name frequency map name
 * @return -1 on failure, idx >= 0 on sucess
 */
int
set_current_freqmap(const char *name) {
    int idx = getfmapidx(name);
    if( idx == -1 )
        return idx;
    curr_fmap = idx;
    logmsg(LOG_NOTICE,"Frequency map set to \"%s\"",name);
    return idx;
}

/**
 * Get the current frequency map, both name and index
 * @param name Buffer set to the name of the current map
 * @param size Maximum size in bytes of buffer
 * @return The current map index
 */
int
get_current_freqmap(char *name, int size) {
    strncpy(name,frequence_map[curr_fmap].name,size);
    name[size-1] = '\0';
    return curr_fmap;
}


/**
 * Get the corresponding frequency map index from a map name
 */
int
getfmapidx(const char *name) {
    for (int i = 0; i < num_fmap; i++) {
        if (stricmp(name, frequence_map[i].name) == 0  && frequence_map[i].size > 0)
            return i;
    }
    return -1;
}

/**
 * Translate a frequency to the standard channel name in the frequency map.
 * Note: it isthe callers responsibility that the parameter fmap is a valid
 * map.
 * @return 0 on successs, -1 otherwise
 */
int
getchfromfreq(char **ch, const unsigned int freq) {
    if (curr_fmap >= num_fmap) {
        return -1;
    }
    for (unsigned int i = 0; i < frequence_map[curr_fmap].size; ++i) {
        if (freq == frequence_map[curr_fmap].tbl[i].freq*1000) {
            *ch = &frequence_map[curr_fmap].tbl[i].ch[0];
            return 0;
        }
    }
    return -1;
}

/**
 * Translate a channel name to a frequency using the specified frequency map
 * @return 0 on successs, -1 otherwise
 */
int
getfreqfromch(unsigned int *freq, const char *ch) {
    if (curr_fmap >= num_fmap) {
        return -1;
    }
    for (unsigned int i = 0; i < frequence_map[curr_fmap].size; ++i) {
        if (stricmp(ch, frequence_map[curr_fmap].tbl[i].ch) == 0 ) {
            *freq = frequence_map[curr_fmap].tbl[i].freq*1000;
            return 0;
        }
    }
    return -1;
}

/**
 * Translate a symbolic name to a frequency. The string can be given as
 * either a station or channel name
 * @param freq
 * @param name
 * @return 0 on success, -1 on failure
 */
int
getfreqfromstr(unsigned int *freq,const char *name) {
    char chbuffer[32];
    // First try if this is a station name
    if( 0 == get_chfromstation(name, chbuffer, 32) ) {
        if( 0 == getfreqfromch(freq, chbuffer) ) {
            return 0;
        }
    }
    else {
        if( 0 == getfreqfromch(freq, name) ) {
            return 0;
        }
    }
    return -1;
}

/*
 * Fill the supplied buffer (array of string pointers) 
 * with pointer to statically alocated buffers with the name of 
 * all the defined frequency maps
 */
int getfmapnames(char *names[], int size) {
    int i;
    for(i=0; i < size && i < num_fmap; i++) {
        names[i] = frequence_map[i].name;
    }
    return i;
}