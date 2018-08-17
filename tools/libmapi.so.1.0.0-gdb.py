# SPDX-License-Identifier: AGPL-3.0-or-later
import datetime
import math
import MAPI
import MAPI.Defs
import MAPI.Tags

reverse_proptags_exact = {}
reverse_proptags = {}

class CURRENCY_printer:
	def __init__(self, value):
		self.value = value

	def to_string(self):
		i = int(self.value["int64"])
		return "%d.%04d" % (i / 10000, i % 10000)

class FILETIME_printer:
	def __init__(self, value):
		self.value = value

	def to_string(self):
		nsec = int(self.value["dwHighDateTime"]) << 32
		nsec += int(self.value["dwLowDateTime"])
		nsec -= 116444736000000000
		nsec /= 10000000
		return datetime.datetime.fromtimestamp(nsec).strftime("%Y-%m-%d %H:%M:%S")

class GUID_printer:
	def __init__(self, value):
		self.value = value

	def to_string(self):
		return str(self.value.cast(gdb.lookup_type("MAPIUID")))

class GUID_p_printer:
	def __init__(self, value):
		self.value = value

	def to_string(self):
		return str(self.value.cast(gdb.lookup_type("MAPIUID").pointer()))

class LARGE_INTEGER_printer:
	def __init__(self, value):
		self.value = value

	def to_string(self):
		return self.value["QuadPart"];

class MAPIUID_printer:
	def __init__(self, value):
		self.value = value

	def to_string(self):
		v = self.value["ab"]
		a = "{"
		a += "%02x" * 4 % (v[0], v[1], v[2], v[3])
		a += "-"
		a += "%02x%02x" % (v[4], v[5])
		a += "-"
		a += "%02x%02x" % (v[6], v[7])
		a += "-"
		a += "%02x" * 8 % (v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15])
		a += "}"
		return a

class MAPIUID_p_printer:
	def __init__(self, value):
		self.value = value

	def to_string(self):
		return str(self.value.address) + " " + str(self.value.dereference())

class SBinary_printer:
	def __init__(self, v):
		self.value = v
		self.voidp = gdb.lookup_type("void").pointer()

	def to_string(self):
		return str(self.value["cb"]) + "B:" + str(self.value["lpb"].cast(self.voidp))

class SPropValue_printer:
	def __init__(self, value):
		self.value = value
		self.voidp = gdb.lookup_type("void").pointer()

	def mv_walk(self, arr, count):
		ret = "[" + str(count) + "]{"
		for i in xrange(0, count):
			if i != 0:
				ret += ", "
			ret += str(arr[i])
		return ret + "}"

	def mv_decode(self):
		type = self.value["ulPropTag"] & 0xFFFF
		v = self.value["Value"]
		if (type == 0x1002):
			return "PT_MV_SHORT" + self.mv_walk(v["MVi"]["lpi"], v["MVi"]["cValues"])
		elif (type == 0x1003):
			return "PT_MV_LONG" + self.mv_walk(v["MVl"]["lpl"], v["MVl"]["cValues"])
		elif (type == 0x1004):
			return "PT_MV_FLOAT" + self.mv_walk(v["MVflt"]["lpflt"], v["MVflt"]["cValues"])
		elif (type == 0x1005):
			return "PT_MV_DOUBLE" + self.mv_walk(v["MVdbl"]["lpdbl"], v["MVdbl"]["cValues"])
		elif (type == 0x1006):
			return "PT_MV_CURRENCY" + self.mv_walk(v["MVcur"]["lpcur"], v["MVcur"]["cValues"])
		elif (type == 0x1007):
			return "PT_MV_APPTIME" + self.mv_walk(v["MVat"]["lpat"], v["MVat"]["cValues"])
		elif (type == 0x1014):
			return "PT_MV_LONGLONG" + self.mv_walk(v["MVli"]["lpli"], v["MVli"]["cValues"])
		elif (type == 0x101E):
			return "PT_MV_STRING8" + self.mv_walk(v["MVszA"]["lppszA"], v["MVszA"]["cValues"])
		elif (type == 0x101F):
			return "PT_MV_UNICODE" + self.mv_walk(v["MVszW"]["lppszW"], v["MVszW"]["cValues"])
		elif (type == 0x1040):
			return "PT_MV_SYSTIME" + self.mv_walk(v["MVft"]["lpft"], v["MVft"]["cValues"])
		elif (type == 0x1048):
			return "PT_MV_CLSID" + self.mv_walk(v["MVguid"]["lpguid"], v["MVguid"]["cValues"])
		elif (type == 0x1102):
			return "PT_MV_BINARY" + self.mv_walk(v["MVbin"]["lpbin"], v["MVbin"]["cValues"])
		return "PT_MV_??"

	def to_string(self):
		tag = ""
		try:
			tag = reverse_proptags_exact[int(self.value["ulPropTag"])]
		except KeyError:
			try:
				tag = reverse_proptags[int(self.value["ulPropTag"]) >> 16]
			except KeyError:
				tag = hex(int(self.value["ulPropTag"]))
		return tag + " " + self.s_decode()

	def s_decode(self):
		type = self.value["ulPropTag"] & 0xFFFF
		v = self.value["Value"]
		if ((type & 0x1000) == 0x1000):
			return self.mv_decode()
		elif (type == 0):
			return "PT_UNSPEC"
		elif (type == 0x1):
			return "PT_NULL " + str(v["x"])
		elif (type == 0x2):
			return "PT_SHORT " + str(v["i"])
		elif (type == 0x3):
			return "PT_LONG " + str(v["l"])
		elif (type == 0x4):
			return "PT_FLOAT " + str(v["flt"])
		elif (type == 0x5):
			return "PT_DOUBLE " + str(v["dbl"])
		elif (type == 0x6):
			return "PT_CURRENCY " + str(v["cur"])
		elif (type == 0x7):
			return "PT_APPTIME " + str(v["at"])
		elif (type == 0xA):
			return "PT_ERROR " + str(v["err"])
		elif (type == 0xB):
			return "PT_BOOLEAN " + str(v["b"])
		elif (type == 0xD):
			return "PT_OBJECT " + str(v["x"])
		elif (type == 0x14):
			return "PT_LONGLONG " + str(v["li"])
		elif (type == 0x1E):
			return "PT_STRING8 " + str(v["lpszA"])
		elif (type == 0x1F):
			return "PT_UNICODE " + str(v["lpszW"])
		elif (type == 0x40):
			return "PT_SYSTIME " + str(v["ft"])
		elif (type == 0x48):
			return "PT_CLSID " + str(v["lpguid"])
		elif (type == 0x102):
			return "PT_BINARY " + str(v["bin"])
		else:
			return "PT_??"

def lookup_type(val):
	dtype = val.type.strip_typedefs()
	dname = str(dtype)
	if dname == "LARGE_INTEGER" or dname == "ULARGE_INTEGER":
		return LARGE_INTEGER_printer(val)
	if dname == "SBinary":
		return SBinary_printer(val)
	if dname == "SPropValue":
		return SPropValue_printer(val)
	if dname == "FILETIME":
		return FILETIME_printer(val)
	if dname == "GUID":
		return GUID_printer(val)
	if dtype == gdb.lookup_type("GUID").pointer():
		return GUID_p_printer(val)
	if dname == "MAPIUID":
		return MAPIUID_printer(val)
	if dtype == gdb.lookup_type("MAPIUID").pointer():
		return MAPIUID_p_printer(val)

	iname = str(val.type)
	if iname == "CURRENCY":
		return CURRENCY_printer(val)
	return None

# main

for key in dir(MAPI.Tags):
	if not key.startswith("PR_"):
		continue
	value = int(getattr(MAPI.Tags, key))
	reverse_proptags_exact[value] = key
	if (MAPI.Defs.PROP_TYPE(value) == MAPI.PT_STRING8 and key.endswith("_A")) or \
	   (MAPI.Defs.PROP_TYPE(value) == MAPI.PT_UNICODE and key.endswith("_W")):
		key = key[:-2]
	reverse_proptags[value >> 16] = key

gdb.pretty_printers.append(lookup_type)
