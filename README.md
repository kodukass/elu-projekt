# REAKTSIOONI LAMP

Materjalid ja juhend reaktsioonilampide jaoks, mis suudavad mõõta täpselt
reaktsiooniaega. Lamp süttib 5 sekundi jooksul pärast testi käivitamist ja kustub, kui lambil olev valgussensor on kaetud. Reaktsioonilamp on juhitav veebilehe kaudu, mis annab ka tagasisidet lampide aktiveerimise kohta ja sisaldab ka seadete lehte. Olemas on ka vastupidav korpus seade ohutuks kasutamiseks.<br>
Kaustas "(WIP) prototüüp mitmele lambile" on kood kasutamiseks ka mitmele erinevale lambile. Kahjuks see ei tööta.

<hr>

<h2>Kuidas kasutada?</h2>


<h3><u>Koostisosad:</u></h3>

<ul>
<li>3D mudel (link allpool)</li>
Filamendi materjalid:
<ul>
<li>Läbipaistev PETG</li>
<li>ABS</li>
</ul>
<li>NodeMCU 1.0 ESP8266</li>
<li>RGB Led lamp</li>
<li>LDR valgussensor</li>
<li>lüliti</li>
<li>9V patarei</li>
<li>4 x 10k takistid</li>
<li>Jootmis tööriistad</li>
<li>13 x eri värvi juhtmed</li>
Soovitused värvidele selguse mõttes:
<ul>
<li>4 x punane</li>
<li>4 x must (maanduste jaoks)</li>
<li>1 x kollane</li>
<li>1 x oranz</li>
<li>2 x roheline</li>
<li>2 x sinine</li>
</ul>
</ul>


<h3>1. Jootke kõik tükid kokku alloleva skeemi põhjal (Joonis 1)<br></h3>(katsetamiseks võib selle jootmata panna ka breadboard'i/prototüüpimis laua peale)

<br>

<img src="juhendi pildid/graph.png" alt="Joonis 1">
<br>
<i>Joonis 1 - juhtmete skeem</i>

<br>

<h3>2. Seejärel printige välja 3D printeriga reaktsiooni lambi füüsiline mudel:

https://www.thingiverse.com/thing:7227706</h3>

See peaks koosnema kolmest tükkist (Joonis 2):
<ul>
<li>Läbipaistev kuppel (valge/läbipaistev)</li>
<li>Valgussensori kinnitus (oranz)</li>
<li>Põhi (tume roheline)</li>
</ul>
<br>

<img src="juhendi pildid/reaktsiooni lamp.jpg" alt="Joonis 2">
<br>
<i>Joonis 2 - 3D mudel kokkupandult</i>

<br>

<h3>3. Liimige valgussensor tema kinnituse külge</h3>
Pange see tüki keskel oleva augu alla. Vajadusel sobib seda suurendada 6'se puuriga. Väga hästi sobib siia kuumliim. Ärge katke sensorit ega auku liimiga! Läbi selle näeb sensor ümbruse heleduse muutumist.

<h3>4. Pange lüliti läbi põhjas oleva ava</h3>
Vajadusel võib selle sinna liimida. Kui see sinna ei mahu sobib kasutada 6'st puuri ja/või kergelt seda lihvida. Lüliti peab jääma suunaga alla (mitte kupli ja juhtmete sisse)

<h3>5. Laadige NodeMCU plaadile kood</h3>
See asub kaustas "elu_kood". Laadige see alla oma arvutisse. Avage fail Arduino programmis ja olles kinnitanud plaadi juhtmega arvuti külge ja valinud plaadi tüübiks "NodeMCU 1.0" laadige kood plaadile.

<h3>6. Pange kõik juhtmed kupli sisse ja keerake kuppel kinni</h3>
<b>NB! Ärge kinnitage valgussensori kinnitus enne keeramist kupli külge. See on kahes osas, et juhtmed ei läheks keeramise ajal katki.</b> Pärast kupli kinni keeramist kinnitage valgussensori kinnitus kupli külge.

<h3>7. Pange lamp lülitist tööle</h3>
Allpool on juhised kuidas pääseda juhtimislehele:

<br>

<hr>

<h3><u>Juhtimislehega ühendumine:</u></h3>

<h3>1. Liituge arvutis või nutitelefonis uue Wi-Fi võrguga "PÕRANDAPOD_WIFI"</h3>
Võrgu parool on <b>"salajane123"</b>

<h3>2. Minge veebilehitsejas leheküljele "192.168.4.1"</h3>
Ärge lisage algusesse "https" või lõppu ".www"

<br>
<hr>

<h3><u>Juhtimislehe kasutamine:</u></h3>

Avaneb lehekülg peab välja nägema sellisena (Joonis 3 ja 4)

<img src="juhendi pildid/juhtleht1.jpg" alt="joonis 3">
<i>Joonis 3 - Juhtimislehekülje esimene pool</i>

<br>

<ul>
<li>1. Katse tegija nime sisestamine ja salvestamine</li>
<li>2. Start nupp katse alustamiseks</li>
<ul>
<li>NB! Enne katse alustamist tuleb ära seadistada valgusanduri tugevus (selle juhend on allpool)</li>
</ul>
<li>3. Lambi hetke olek</li>
<li>4. Viimane salvestatud aeg (võib vajada lehekülje uuendamist)</li>
</ul>

<br>

<img src="juhendi pildid/juhtleht2.jpg" alt="joonis 4">
<i>Joonis 4 - Juhtimislehekülje teine pool</i>

<br>

<ul>
<li>5. Edetabel kõigi tulemuste jaoks (võib samuti vajada lehekülje uuendamist)</li>
<li>6. Nupud, et allalaadida tulemused CSV failina ja edetabeli kustutamiseks</li>
<li>7. Valgusanduri seadete nupp</li>
<li>8. Värvi valimise nupp. Valikus on kollane ja roheline</li>
<li>9. Hetkene anduri sisend ja olek</li>
</ul>

<br>
<hr>

<h3><u>Seadete leht</u></h3>

Kui vajutada lingile "Anduri seaded" (7), viib see teid leheküljele, kus saab seadistada valgusanduri tugevust.

<img src="juhendi pildid/seaded.jpg" alt="joonis 5">
<i>Joonis 5 - Anduri seadete leht</i>

<br>

OFF lävi (1) vaatab, millise valguse taseme juures tuleb panna lamp kinni ja ON lävi (2) vaatab, millise valgustaseme juures tuleb lamp hoida töös. Neid peab saama muuta, et erineva pimedusega ruumides oleks võimalik lambil töödata.
<br>
Kõige lihtsam viis kuidas neid seadistada on katta lamp käega ja vaadata anduri väljundist (4), mis number on "LVL" ja panna sellele ligilähedane number OFF läviks. Samamoodi saab seadistada ka ON lävi, kui võtta ära kõik takistused anduri eest ja vaadata, mis on "LVL" väärtus. Vajadusel saab ka taastada vaikeseaded (3)

<hr>
<h3><u>Head kasutamist!</u></h3>

Seda koodi ja 3D mudelit saab vabavaraliselt kasutada igaüks, kes seda soovib. :)
<hr>

*See seade on tehtud Tallinna Ülikooli ELU projekti raames*
