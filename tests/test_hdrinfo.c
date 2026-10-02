/*
 * Host-тест разбора вывода `meson_display_client -r dumpDisplayAttribute`.
 * Фикстура — фрагмент реального дампа (ATV14, .50, 2026-10-02), вместе с
 * «соседями», на которых парсер ломается чаще всего: многострочное значение
 * HDR CAP, FORCE HDR Mode (содержит в себе HDR Mode) и Dolby Vision
 * HDR 10 Policy (содержит в себе HDR Policy).
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../hdrinfo.h"

static const char *fixture =
	"Dump display attribute:\n"
	"{\n"
	"\t\"Dolby Vision Enable\" : \n"
	"\t{\n"
	"\t\t\"id\" : 2,\n"
	"\t\t\"value\" : \"N\"\n"
	"\t},\n"
	"\t\"FORCE HDR Mode\" : \n"
	"\t{\n"
	"\t\t\"id\" : 17,\n"
	"\t\t\"value\" : \"0\"\n"
	"\t},\n"
	"\t\"HDR CAP\" : \n"
	"\t{\n"
	"\t\t\"id\" : 9,\n"
	"\t\t\"value\" : \"HDR10Plus Supported: 0\n"
	"HDR Static Metadata:\n"
	"    Supported EOTF:\n"
	"        SMPTE ST 2084: 0\"\n"
	"\t},\n"
	"\t\"HDR Mode\" : \n"
	"\t{\n"
	"\t\t\"id\" : 11,\n"
	"\t\t\"value\" : \"2\"\n"
	"\t},\n"
	"\t\"HDR Policy\" : \n"
	"\t{\n"
	"\t\t\"id\" : 10,\n"
	"\t\t\"value\" : \"0\"\n"
	"\t},\n"
	"\t\"HDR Priority\" : \n"
	"\t{\n"
	"\t\t\"id\" : 16,\n"
	"\t\t\"value\" : \"268435472\"\n"
	"\t},\n"
	"\t\"Dolby Vision HDR 10 Policy\" : \n"
	"\t{\n"
	"\t\t\"id\" : 7,\n"
	"\t\t\"value\" : \"0\"\n"
	"\t},\n"
	"\t\"SDR Mode\" : \n"
	"\t{\n"
	"\t\t\"id\" : 12,\n"
	"\t\t\"value\" : \"0\"\n"
	"\t}\n"
	"}\n";

int main (void)
{
	hdrinfo_t info;

	/* Полный дамп: все пять полей на месте, соседи не путаются */
	assert (hdrinfo_parse (fixture, &info));
	assert (info.valid);
	assert (info.mode == 2);
	assert (info.policy == 0);
	assert (info.priority == 268435472);
	assert (info.sdr_mode == 0);
	assert (info.force_hdr == 0);

	/* Пустой ввод и ввод без наших атрибутов — «не разобрано» */
	assert (!hdrinfo_parse ("", &info));
	assert (!info.valid);
	assert (!hdrinfo_parse ("Dump display attribute:\n{\n}\n", &info));
	assert (!info.valid);

	/* «Dolby Vision HDR 10 Policy» — не «HDR Policy»: разбирать её нельзя */
	assert (!hdrinfo_parse ("\t\"Dolby Vision HDR 10 Policy\" : \n\t{\n\t\t\"id\" : 7,\n"
	                        "\t\t\"value\" : \"3\"\n\t},\n", &info));
	assert (!info.valid);

	/* «FORCE HDR Mode» — не «HDR Mode»: попадает только в своё поле */
	assert (hdrinfo_parse ("\t\"FORCE HDR Mode\" : \n\t{\n\t\t\"id\" : 17,\n"
	                       "\t\t\"value\" : \"1\"\n\t},\n", &info));
	assert (info.force_hdr == 1);
	assert (info.mode == 0);

	/* Нечисловое значение не разбирается (Dolby Vision Enable = "N") */
	assert (!hdrinfo_parse ("\t\"HDR Mode\" : \n\t{\n\t\t\"id\" : 11,\n"
	                        "\t\t\"value\" : \"N\"\n\t},\n", &info));
	assert (!info.valid);

	/* Значение с хвостовыми пробелами — числовое */
	assert (hdrinfo_parse ("\t\"SDR Mode\" : \n\t{\n\t\t\"id\" : 12,\n"
	                       "\t\t\"value\" : \"4 \"\n\t},\n", &info));
	assert (info.sdr_mode == 4);

	/* Нечитаемый клиент: hdrinfo_refresh с пустым/нулевым cli не портит данные */
	hdrinfo_refresh (NULL);
	assert (!hdrinfo_get ()->valid);
	hdrinfo_refresh ("");
	assert (!hdrinfo_get ()->valid);

	printf ("hdrinfo: OK\n");
	return 0;
}
