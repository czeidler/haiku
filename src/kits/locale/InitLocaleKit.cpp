/*
 * This file contains library initialization code.
 * The required mimetypes and attribute-indices are created here.
 */

#include <fs_attr.h>
#include <fs_index.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <DefaultCatalog.h>
#include <MutableLocaleRoster.h>
#include <SystemCatalog.h>


namespace BPrivate {

BCatalog gSystemCatalog;

}


using BPrivate::DefaultCatalog;
using BPrivate::MutableLocaleRoster;
using BPrivate::gSystemCatalog;


// helper function that makes sure an attribute-index exists:
static void EnsureIndexExists(const char *attrName)
{
	BVolume bootVol;
	BVolumeRoster volRoster;
	if (volRoster.GetBootVolume(&bootVol) != B_OK)
		return;
	struct index_info idxInfo;
	if (fs_stat_index(bootVol.Device(), attrName, &idxInfo) != 0)
		fs_create_index(bootVol.Device(), attrName, B_STRING_TYPE, 0);
}


/*
 * prepares the system for use by the Locale Kit catalogs,
 * it makes sure that the required indices and mimetype exist:
 */
static void
SetupCatalogBasics()
{
	// make sure the indices required for catalog-traversal are there:
	EnsureIndexExists(BLocaleRoster::kCatLangAttr);
	EnsureIndexExists(BLocaleRoster::kCatSigAttr);

	// install mimetype for default-catalog:
	BMimeType mt;
	status_t res = mt.SetTo(DefaultCatalog::kCatMimeType);
	if (res == B_OK && !mt.IsInstalled()) {
		// install supertype, if it isn't available
		BMimeType supertype;
		res = mt.GetSupertype(&supertype);
		if (res == B_OK && !supertype.IsInstalled()) {
			res = supertype.Install();
		}

		if (res == B_OK) {
			// info about the attributes of a catalog...
			BMessage attrMsg;
			// ...the catalog signature...
			attrMsg.AddString("attr:public_name", "Signature");
			attrMsg.AddString("attr:name", BLocaleRoster::kCatSigAttr);
			attrMsg.AddInt32("attr:type", B_STRING_TYPE);
			attrMsg.AddBool("attr:editable", false);
			attrMsg.AddBool("attr:viewable", true);
			attrMsg.AddBool("attr:extra", false);
			attrMsg.AddInt32("attr:alignment", 0);
			attrMsg.AddInt32("attr:width", 140);
			// ...the catalog language...
			attrMsg.AddString("attr:public_name", "Language");
			attrMsg.AddString("attr:name", BLocaleRoster::kCatLangAttr);
			attrMsg.AddInt32("attr:type", B_STRING_TYPE);
			attrMsg.AddBool("attr:editable", false);
			attrMsg.AddBool("attr:viewable", true);
			attrMsg.AddBool("attr:extra", false);
			attrMsg.AddInt32("attr:alignment", 0);
			attrMsg.AddInt32("attr:width", 60);
			// ...and the catalog fingerprint...
			attrMsg.AddString("attr:public_name", "Fingerprint");
			attrMsg.AddString("attr:name", BLocaleRoster::kCatFingerprintAttr);
			attrMsg.AddInt32("attr:type", B_INT32_TYPE);
			attrMsg.AddBool("attr:editable", false);
			attrMsg.AddBool("attr:viewable", true);
			attrMsg.AddBool("attr:extra", false);
			attrMsg.AddInt32("attr:alignment", 0);
			attrMsg.AddInt32("attr:width", 70);
			res = mt.SetAttrInfo(&attrMsg);
		}

		if (res == B_OK) {
			// file extensions (.catalog):
			BMessage extMsg;
			extMsg.AddString("extensions", "catalog");
			res = mt.SetFileExtensions(&extMsg);
		}

		if (res == B_OK) {
			// short and long descriptions:
			mt.SetShortDescription("Translation Catalog");
			res = mt.SetLongDescription("Catalog with translated application resources");
		}

		if (res == B_OK)
			res = mt.Install();
	}
}


void
__initialize_locale_kit()
{
	SetupCatalogBasics();

	MutableLocaleRoster::Default()->LoadSystemCatalog(&gSystemCatalog);
}
