/*
 * YARA rules for x64 PE compiler detection.
 * Copyright (c) 2017 Avast Software, licensed under the MIT license
 */

import "pe"

rule aut2exe_3300 {
	meta:
		tool = "C"
		name = "Aut2Exe"
		version = "3.3.0.0"
		language = "AutoIt"
		bytecode = true
		source = "Made by RetDec Team"
		pattern = "40534883EC20488D057B600900488BD9488901E8F8000000488B4B084883C4205BE9CE750100CCCCCCCCCCCCCCCCCCCC4883EC28488D0DF5EA0B00E820010000833D59EB0B0000763748895C243033DB48897C2420488BFB488B0D39EB0B00488B0C0FE8"
		absoluteStart = 1024
	strings:
		$1 = { 40 53 48 83 EC 20 48 8D 05 7B 60 09 00 48 8B D9 48 89 01 E8 F8 00 00 00 48 8B 4B 08 48 83 C4 20 5B E9 CE 75 01 00 CC CC CC CC CC CC CC CC CC CC 48 83 EC 28 48 8D 0D F5 EA 0B 00 E8 20 01 00 00 83 3D 59 EB 0B 00 00 76 37 48 89 5C 24 30 33 DB 48 89 7C 24 20 48 8B FB 48 8B 0D 39 EB 0B 00 48 8B 0C 0F E8 }
	condition:
		$1 at 0x400
}

rule aut2exe_338x {
	meta:
		tool = "C"
		name = "Aut2Exe"
		version = "3.3.8.x"
		language = "AutoIt"
		bytecode = true
		source = "Made by RetDec Team"
		pattern = "4883EC28833DB5200C00000F85DFBB02004883C428C3CCCCCCCCCCCCCCCCCCCC40534883EC20488BD9E86200000084C00F841AB80200488B43104883C4205BC34883EC284883791000753648895C2420488BD9488D0D6E660900FF15B825090048890348"
		absoluteStart = 1024
	strings:
		$1 = { 48 83 EC 28 83 3D B5 20 0C 00 00 0F 85 DF BB 02 00 48 83 C4 28 C3 CC CC CC CC CC CC CC CC CC CC 40 53 48 83 EC 20 48 8B D9 E8 62 00 00 00 84 C0 0F 84 1A B8 02 00 48 8B 43 10 48 83 C4 20 5B C3 48 83 EC 28 48 83 79 10 00 75 36 48 89 5C 24 20 48 8B D9 48 8D 0D 6E 66 09 00 FF 15 B8 25 09 00 48 89 03 48 }
	condition:
		$1 at 0x400
}

rule aut2exe_33100 {
	meta:
		tool = "C"
		name = "Aut2Exe"
		version = "3.3.10.0"
		language = "AutoIt"
		bytecode = true
		source = "Made by RetDec Team"
		pattern = "488BC44889580848896810488970184889782041564883EC20488B7C24504D8BD9458BF08BDA488BF14C8BD1418BD6498BCAE8290000004C8BD04885C00F85C9430900488B5C2430488B6C2438488B742440488B7C24484883C420415EC3CCCC448BD2EB"
		absoluteStart = 1024
	strings:
		$1 = { 48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 48 8B 7C 24 50 4D 8B D9 45 8B F0 8B DA 48 8B F1 4C 8B D1 41 8B D6 49 8B CA E8 29 00 00 00 4C 8B D0 48 85 C0 0F 85 C9 43 09 00 48 8B 5C 24 30 48 8B 6C 24 38 48 8B 74 24 40 48 8B 7C 24 48 48 83 C4 20 41 5E C3 CC CC 44 8B D2 EB }
	condition:
		$1 at 0x400
}

rule aut2exe_33102 {
	meta:
		tool = "C"
		name = "Aut2Exe"
		version = "3.3.10.2"
		language = "AutoIt"
		bytecode = true
		source = "Made by RetDec Team"
		pattern = "48895C240855565741544155415641574883EC60488B7A1048836424500083CDFF498BD8488BF24C8BF1448BFD448BE5448BED89AC24B800000089AC24A80000004883FF070F87319608004883FF067610488B4608488B4830E8F2D10000448BF84883FF"
		absoluteStart = 1024
	strings:
		$1 = { 48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 8B 7A 10 48 83 64 24 50 00 83 CD FF 49 8B D8 48 8B F2 4C 8B F1 44 8B FD 44 8B E5 44 8B ED 89 AC 24 B8 00 00 00 89 AC 24 A8 00 00 00 48 83 FF 07 0F 87 31 96 08 00 48 83 FF 06 76 10 48 8B 46 08 48 8B 48 30 E8 F2 D1 00 00 44 8B F8 48 83 FF }
	condition:
		$1 at 0x400
}

rule aut2exe_33140 {
	meta:
		tool = "C"
		name = "Aut2Exe"
		version = "3.3.14.0"
		language = "AutoIt"
		bytecode = true
		source = "Made by RetDec Team"
		pattern = "4883EC28488D0DB5F20D00E870040400488D0DC50904004883C428E9345A02004883EC28E8538C0100488D0DB80904004883C428E91B5A0200CCCCCC4883EC28E8278E0100488D0DA80904004883C428E9FF590200CCCCCC488D0D9D090400E9F0590200"
		absoluteStart = 1024
	strings:
		$1 = { 48 83 EC 28 48 8D 0D B5 F2 0D 00 E8 70 04 04 00 48 8D 0D C5 09 04 00 48 83 C4 28 E9 34 5A 02 00 48 83 EC 28 E8 53 8C 01 00 48 8D 0D B8 09 04 00 48 83 C4 28 E9 1B 5A 02 00 CC CC CC 48 83 EC 28 E8 27 8E 01 00 48 8D 0D A8 09 04 00 48 83 C4 28 E9 FF 59 02 00 CC CC CC 48 8D 0D 9D 09 04 00 E9 F0 59 02 00 }
	condition:
		$1 at 0x400
}

rule aut2exe_33142 {
	meta:
		tool = "C"
		name = "Aut2Exe"
		version = "3.3.14.2"
		language = "AutoIt"
		bytecode = true
		source = "Made by RetDec Team"
		pattern = "4883EC28488D0DB5020E00E8B0060400488D0D050C04004883C428E9745C02004883EC28E8738E0100488D0DF80B04004883C428E95B5C0200CCCCCC4883EC28E847900100488D0DE80B04004883C428E93F5C0200CCCCCC488D0DDD0B0400E9305C0200"
		absoluteStart = 1024
	strings:
		$1 = { 48 83 EC 28 48 8D 0D B5 02 0E 00 E8 B0 06 04 00 48 8D 0D 05 0C 04 00 48 83 C4 28 E9 74 5C 02 00 48 83 EC 28 E8 73 8E 01 00 48 8D 0D F8 0B 04 00 48 83 C4 28 E9 5B 5C 02 00 CC CC CC 48 83 EC 28 E8 47 90 01 00 48 8D 0D E8 0B 04 00 48 83 C4 28 E9 3F 5C 02 00 CC CC CC 48 8D 0D DD 0B 04 00 E9 30 5C 02 00 }
	condition:
		$1 at 0x400
}

rule aut2exe_33143 {
	meta:
		tool = "C"
		name = "Aut2Exe"
		version = "3.3.14.3"
		language = "AutoIt"
		bytecode = true
		source = "Made by RetDec Team"
		pattern = "4883EC28488D0DB5220E00E8C0070400488D0D150D04004883C428E9845D02004883EC28E8738E0100488D0D080D04004883C428E96B5D0200CCCCCC4883EC28E847900100488D0DF80C04004883C428E94F5D0200CCCCCC488D0DED0C0400E9405D0200"
		absoluteStart = 1024
	strings:
		$1 = { 48 83 EC 28 48 8D 0D B5 22 0E 00 E8 C0 07 04 00 48 8D 0D 15 0D 04 00 48 83 C4 28 E9 84 5D 02 00 48 83 EC 28 E8 73 8E 01 00 48 8D 0D 08 0D 04 00 48 83 C4 28 E9 6B 5D 02 00 CC CC CC 48 83 EC 28 E8 47 90 01 00 48 8D 0D F8 0C 04 00 48 83 C4 28 E9 4F 5D 02 00 CC CC CC 48 8D 0D ED 0C 04 00 E9 40 5D 02 00 }
	condition:
		$1 at 0x400
}

rule aut2exe_uv_01 {
	meta:
		tool = "C"
		name = "Aut2Exe"
		language = "AutoIt"
		bytecode = true
	strings:
		$1 = ">AUTOIT SCRIPT<"
		$2 = ">AUTOIT SCRIPT<" wide
		$3 = ">AUTOIT UNICODE SCRIPT<" wide
	condition:
		pe.is_64bit() and
		for 1 of them : (
			@ > pe.sections[pe.section_index(".rdata")].raw_data_offset and
			@ < pe.sections[pe.section_index(".rdata")].raw_data_offset +
			pe.sections[pe.section_index(".rdata")].raw_data_size
		)
}

rule autohotkey_uv_01 {
	meta:
		tool = "C"
		name = "AHK2Exe"
		language = "AutoHotKey"
		bytecode = true
	strings:
		$0 = "Hotkeys/hotstrings are not allowed inside functions." wide ascii
		$1 = "IfWin should be #IfWin." wide ascii
		$2 = "This hotstring is missing its abbreviation." wide ascii
		$3 = "Duplicate hotkey." wide ascii
		$4 = ">AUTOHOTKEY SCRIPT<" wide ascii
    condition:
        pe.is_64bit() 
		and 
		pe.number_of_resources > 0 
		and ((
					(@4 > pe.sections[pe.section_index(".rdata")].raw_data_offset 
					and
					@4 < pe.sections[pe.section_index(".rdata")].raw_data_offset +
					pe.sections[pe.section_index(".rdata")].raw_data_size) 
				or
				(for 1 i in (0 .. pe.number_of_resources) : (
					pe.resources[i].name_string matches />AUTOHOTKEY SCRIPT</))
			)
			or 
			(3 of ($0,$1,$2,$3))
		)
}

rule f2ko_bat2exe_uv_01 {
	meta:
		tool = "C"
		name = "F2KO Bat2Exe"
		pattern = "4883EC??49C7C0????????4831D248B9????????????????E8????????4831C9E8????????4889??????????4D31C048C7C2001000004831C9E8????????4889"
	strings:
		$1 = { 48 83 EC ?? 49 C7 C0 ?? ?? ?? ?? 48 31 D2 48 B9 ?? ?? ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 31 C9 E8 ?? ?? ?? ?? 48 89 ?? ?? ?? ?? ?? 4D 31 C0 48 C7 C2 00 10 00 00 48 31 C9 E8 ?? ?? ?? ?? 48 89 }
	condition:
		$1 at pe.entry_point
}

rule msvc_general
{
	meta:
		tool = "C"
		name = "MSVC"
		source = "Made by RetDec Team"
		pattern = "4883EC28E8????00004883C428E9??FEFFFFCCCC"
	strings:
		$1 = { 48 83 EC 28 E8 ?? ?? 00 00 48 83 C4 28 E9 ?? FE FF FF CC CC }
	condition:
		$1 at pe.entry_point
}

rule gc
{
	meta:
		tool = "C"
		name = "gc"
		language = "Go"
		pattern = "488D742408488B3C24488D0510000000FFE0????????????????????????????488D05?9C?FFFFFFE0??????????????51488B01488B7110488B490865488B3C2530000000C74768000000004881EC8000000083F9047E1183F9107E02CD034889E7FCF348A54889E6488B0E488B56084C8B46104C8B4E18"
		strings:
		$1 = { 48 8D 74 24 08 48 8B 3C 24 48 8D 05 10 00 00 00 FF E0 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 48 8D 05 ?9 C? FF FF FF E0 ?? ?? ?? ?? ?? ?? ?? 51 48 8B 01 48 8B 71 10 48 8B 49 08 65 48 8B 3C 25 30 00 00 00 C7 47 68 00 00 00 00 48 81 EC 80 00 00 00 83 F9 04 7E 11 83 F9 10 7E 02 CD 03 48 89 E7 FC F3 48 A5 48 89 E6 48 8B 0E 48 8B 56 08 4C 8B 46 10 4C 8B 4E 18 }
	condition:
		$1 at pe.entry_point
}

rule gc_mingw
{
	meta:
		tool = "C"
		name = "gc"
		language = "Go"
		pattern = "4883EC28488B05?5????00C70000000000E8?A????00E895FCFFFF90904883C428C39090909090909090909090909090554889E55DC3662E0F1F840000000000554889E54883EC2048833D?0????00007430488D0DA7?A??00FF15??????004885C0742F"
		strings:
		$1 = { 48 83 EC 28 48 8B 05 ?5 ?? ?? 00 C7 00 00 00 00 00 E8 ?A ?? ?? 00 E8 95 FC FF FF 90 90 48 83 C4 28 C3 90 90 90 90 90 90 90 90 90 90 90 90 90 90 55 48 89 E5 5D C3 66 2E 0F 1F 84 00 00 00 00 00 55 48 89 E5 48 83 EC 20 48 83 3D ?0 ?? ?? 00 00 74 30 48 8D 0D A7 ?A ?? 00 FF 15 ?? ?? ?? 00 48 85 C0 74 2F }
	condition:
		$1 at pe.entry_point
}
