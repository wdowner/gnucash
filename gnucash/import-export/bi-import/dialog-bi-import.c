/*
 * dialog-bi-import.c -- Invoice importer Core functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652
 * Boston, MA  02110-1301,  USA       gnu@gnu.org
 */

/**
 * @internal
 * @file dialog-bi-import.c
 * @brief core import functions for invoice import plugin
 * @author Copyright (C) 2009 Sebastian Held <sebastian.held@gmx.de>
 * @author Mike Evans <mikee@saxicola.co.uk>
 * @todo Create an option to import a pre-formed regex when it is present
 * to enable the use of custom output csv formats.
 * @todo Open the newly created invoice(es).
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <regex.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "gnc-date.h"
#include "gnc-ui.h"
#include "gnc-ui-util.h"
#include "gnc-gui-query.h"
#include "gncAddress.h"
#include "gncVendorP.h"
#include "gncVendor.h"
#include "gncEntry.h"
#include "gnc-prefs.h"

#include "gnc-exp-parser.h"

// query
#include "Query.h"
#include "qof.h"
#include "gncIDSearch.h"
#include "dialog-bi-import.h"
#include "dialog-bi-import-helper.h"

// To open the invoices for editing
#include "gnc-plugin-page-invoice.h"
#include "dialog-invoice.h"
#include "business-gnome-utils.h"

// this helper macro takes a regexp match and fills the model
#define FILL_IN_HELPER(match_name,column) \
            temp = g_match_info_fetch_named (match_info, match_name); \
            if (temp) \
            { \
                g_strstrip( temp ); \
                gtk_list_store_set (store, &iter, column, temp, -1); \
                g_free (temp); \
            }

static QofLogModule log_module = G_LOG_DOMAIN; //G_LOG_BUSINESS;
static char * un_escape(char *str);
bi_import_result
gnc_bi_import_read_file (const gchar * filename, const gchar * parser_regexp,
                         GtkListStore * store, guint max_rows,
                         bi_import_stats * stats)
{
    // some statistics
    bi_import_stats stats_fallback;
    FILE *f;

    // regexp
    char *line = NULL;
    gchar *line_utf8 = NULL;
    gchar *temp = NULL;
    GMatchInfo *match_info;
    GError *err;
    GRegex *regexpat;

    // model
    GtkTreeIter iter;

    f = g_fopen (filename, "rt");
    if (!f)
    {
        //gnc_error_dialog (NULL, _("File %s cannot be opened."), filename );
        return RESULT_OPEN_FAILED;
    }

    // set up statistics
    if (!stats)
        stats = &stats_fallback;

    // compile the regular expression and check for errors
    err = NULL;
    regexpat =
        g_regex_new (parser_regexp, G_REGEX_EXTENDED | G_REGEX_OPTIMIZE | G_REGEX_DUPNAMES, 0, &err);
    if (err != NULL)
    {
        GtkWidget *dialog;
        gchar *errmsg;

        errmsg = g_strdup_printf (_("Error in regular expression '%s':\n%s"),
                                  parser_regexp, err->message);
        g_error_free (err);
        err = NULL;

        dialog = gtk_message_dialog_new (NULL,
                                         GTK_DIALOG_MODAL,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_OK, "%s", errmsg);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        g_free (errmsg);
        errmsg = 0;

        fclose (f);
        return RESULT_ERROR_IN_REGEXP;
    }

    // start the import
    stats->n_imported = 0;
    stats->n_ignored = 0;
    stats->ignored_lines = g_string_new (NULL);
#define buffer_size 1000
    line = g_malloc0 (buffer_size);
    while (!feof (f)
            && ((max_rows == 0)
                || (stats->n_imported + stats->n_ignored < max_rows)))
    {
        int l;
        // read one line
        if (!fgets (line, buffer_size, f))
            break;			// eof
        // now strip the '\n' from the end of the line
        l = strlen (line);
        if ((l > 0) && (line[l - 1] == '\n'))
            line[l - 1] = 0;

        // convert line from locale into utf8
        line_utf8 = g_locale_to_utf8 (line, -1, NULL, NULL, NULL);

        // parse the line
        match_info = NULL;	// it seems, that in contrast to documentation, match_info is not alsways set -> g_match_info_free will segfault
        if (g_regex_match (regexpat, line_utf8, 0, &match_info))
        {
            // match found
            stats->n_imported++;

            // fill in the values
            gtk_list_store_append (store, &iter);
            FILL_IN_HELPER ("id", ID); /* FIXME: Should "id" be translated? I don't think so. */
            FILL_IN_HELPER ("date_opened", DATE_OPENED);
            FILL_IN_HELPER ("owner_id", OWNER_ID);
            FILL_IN_HELPER ("billing_id", BILLING_ID);
            FILL_IN_HELPER ("notes", NOTES);

            FILL_IN_HELPER ("date", DATE);
            FILL_IN_HELPER ("desc", DESC);
            FILL_IN_HELPER ("action", ACTION);
            FILL_IN_HELPER ("account", ACCOUNT);
            FILL_IN_HELPER ("quantity", QUANTITY);
            FILL_IN_HELPER ("price", PRICE);
            FILL_IN_HELPER ("disc_type", DISC_TYPE);
            FILL_IN_HELPER ("disc_how", DISC_HOW);
            FILL_IN_HELPER ("discount", DISCOUNT);
            FILL_IN_HELPER ("taxable", TAXABLE);
            FILL_IN_HELPER ("taxincluded", TAXINCLUDED);
            FILL_IN_HELPER ("tax_table", TAX_TABLE);

            FILL_IN_HELPER ("date_posted", DATE_POSTED);
            FILL_IN_HELPER ("due_date", DUE_DATE);
            FILL_IN_HELPER ("account_posted", ACCOUNT_POSTED);
            FILL_IN_HELPER ("memo_posted", MEMO_POSTED);
            FILL_IN_HELPER ("accu_splits", ACCU_SPLITS);
        }
        else
        {
            // ignore line
            stats->n_ignored++;
            g_string_append (stats->ignored_lines, line_utf8);
            g_string_append_c (stats->ignored_lines, '\n');
        }

        g_match_info_free (match_info);
        match_info = 0;
        g_free (line_utf8);
        line_utf8 = 0;
    }
    g_free (line);
    line = 0;

    g_regex_unref (regexpat);
    regexpat = 0;
    fclose (f);

    if (stats == &stats_fallback)
        // stats are not requested -> free the string
        g_string_free (stats->ignored_lines, TRUE);

    return RESULT_OK;
}


//! \brief try to fix some common errors in the csv representation of invoices
//! * corrects the date format
//! * corrects ambiguous values in multi line invoices
//! * ensures customer exists
//! * if quantity is unset, set to 1
//! * if price is unset, delete row.
void
gnc_bi_import_fix_bis (GtkListStore * store, guint * fixed, guint * deleted,
                       GString * info, gchar *type)
{
    GtkTreeIter iter, first_row_of_invoice;
    gboolean valid, row_fixed, on_first_row_of_invoice, ignore_invoice;
    gchar *id = NULL, *date_opened = NULL, *date_posted = NULL, *due_date = NULL, *account_posted = NULL,
    *owner_id = NULL, *date = NULL, *account = NULL, *quantity = NULL, *price = NULL;
    GString *running_id;
    Account *acc = NULL;
    guint dummy;
    gint row = 1, fixed_for_invoice = 0;
    const gchar* date_format_string = qof_date_format_get_string (qof_date_format_get()); // Get the user set date format string


    //date_format_string = qof_date_format_get_string (qof_date_format_get());

    DEBUG("date_format_string: %s",date_format_string);
    // allow the call to this function with only GtkListeStore* specified
    if (!fixed)
        fixed = &dummy;
    if (!deleted)
        deleted = &dummy;

    *fixed = 0;
    *deleted = 0;
    
    // Init control variables
    running_id = g_string_new("");
    ignore_invoice = FALSE;
    on_first_row_of_invoice = TRUE;

    // Walk through the list, reading each row.
    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);
    while (valid)
    {
        row_fixed = FALSE;

        //  If this is a row for a new invoice id, validate header values.
        if (on_first_row_of_invoice)
        {
            gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
                                ID, &id,
                                DATE_OPENED, &date_opened,
                                DATE_POSTED, &date_posted,
                                DUE_DATE, &due_date,
                                ACCOUNT_POSTED, &account_posted,
                                OWNER_ID, &owner_id, -1);
            
            g_string_assign (running_id, id);
            first_row_of_invoice = iter;
            
            // Validate the invoice id.
            if (strlen (id) == 0)
            {
                // The id in the first row of an invoice is blank, ignore the invoice
                ignore_invoice = TRUE;
                g_string_append_printf (info,
                                        _("Row %d: invoice ignored, invoice ID not set.\n"), row);
            }

            // Validate customer or vendor.
            if (!ignore_invoice && strlen (owner_id) == 0)
            {
                ignore_invoice = TRUE;
                g_string_append_printf (info,
                                        _("Row %d: invoice %s ignored, owner not set.\n"),
                                        row, id);
            }
            // Verify that customer or vendor exists.
            if (!ignore_invoice)
            {
                if (g_ascii_strcasecmp (type, "BILL") == 0)
                {
                    if (!gnc_search_vendor_on_id
                        (gnc_get_current_book (), owner_id))
                    {
                        // Vendor not found.
                        ignore_invoice = TRUE;
                        g_string_append_printf (info,
                                                _("Row %d: invoice %s ignored, vendor %s does not exist.\n"),
                                                row, id, owner_id);
                    }
                }
                else if (g_ascii_strcasecmp (type, "INVOICE") == 0)
                {
                    if (!gnc_search_customer_on_id
                        (gnc_get_current_book (), owner_id))
                    {
                        // Customer not found.
                        ignore_invoice = TRUE;
                        g_string_append_printf (info,
                                                _("Row %d: invoice %s ignored, customer %s does not exist.\n"),
                                                row, id, owner_id);
                    }
                }
            }
            
            if (!ignore_invoice && strlen(date_posted) != 0)
            {
                // Validate the date posted.
                if (!isDateValid(date_posted))
                {
                    // Invalid date posted in first row of invoice, ignore the invoice
                    ignore_invoice = TRUE;
                    g_string_append_printf (info,
                                            _("Row %d: invoice %s ignored, %s is not a valid posting date.\n"),
                                            row, id, date_posted);
                }
                
                // Validate account posted.
                // Account should exists, and should be of type A/R for invoices, A/P for bills.
                if (!ignore_invoice)
                {
                    acc = gnc_account_lookup_for_register
                    (gnc_get_current_root_account (), account_posted);
                    if (acc == NULL)
                    {
                        ignore_invoice = TRUE;
                        g_string_append_printf (info,
                                                _("Row %d: invoice %s ignored, account %s does not exist.\n"),
                                                row, id,account_posted);
                    }
                    else
                    {
                        if (g_ascii_strcasecmp (type, "BILL") == 0)
                        {
                            
                            if (xaccAccountGetType (acc) != ACCT_TYPE_PAYABLE)
                            {
                                ignore_invoice = TRUE;
                                g_string_append_printf (info,
                                                        _("Row %d: invoice %s ignored, account %s is not of type Accounts Payable.\n"),
                                                        row, id, account_posted);
                            }
                        }
                        else if (g_ascii_strcasecmp (type, "INVOICE") == 0)
                        {
                            if (xaccAccountGetType (acc) != ACCT_TYPE_RECEIVABLE)
                            {
                                ignore_invoice = TRUE;
                                g_string_append_printf (info,
                                                        _("Row %d: invoice %s ignored, account %s is not of type Accounts Receivable.\n"),
                                                        row, id, account_posted);
                            }
                        }
                    }
                }
                
                // Verify the due date.
                if (!ignore_invoice && !isDateValid(due_date))
                {
                    // Fix this by using the date posted.
                    gtk_list_store_set (store, &iter, DUE_DATE,
                                        date_posted, -1);
                    row_fixed = TRUE;
                }
            }
            
            // Verify the date opened.
            if(!ignore_invoice && !isDateValid(date_opened))
            {
                // Fix this by using the current date.
                gchar temp[20];
                GDate date;
                g_date_clear (&date, 1);
                gnc_gdate_set_today (&date);
                g_date_strftime (temp, 20, date_format_string, &date);    // Create a user specified date string.
                gtk_list_store_set (store, &iter, DATE_OPENED,
                                    temp, -1);
                row_fixed = TRUE;
            }
        }
        
        // Validate and fix item date for each row.
        
        // Get item data.
        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
                            DATE, &date,
                            ACCOUNT, &account,
                            QUANTITY, &quantity,
                            PRICE, &price, -1);

        
        // Validate the price.
        if (strlen (price) == 0)
        {
            // No valid price, delete the row
            ignore_invoice = TRUE;
            g_string_append_printf (info,
                                    _("Row %d: invoice %s ignored, price not set.\n"),
                                    row, id);
        }

        // Validate the account
        if (!ignore_invoice)
        {
            acc = gnc_account_lookup_for_register (gnc_get_current_root_account (),
                                                   account);
            if (acc == NULL)
            {
                ignore_invoice = TRUE;
                g_string_append_printf (info,
                                        _("Row %d: invoice %s ignored, account %s does not exist.\n"),
                                        row, id,account);
            }
        }
        
        // Fix item data.
        if (!ignore_invoice)
        {
            
            // Verify the quantity.
            if (strlen (quantity) == 0)
            {
                // The quantity is not set, default to 1.
                gtk_list_store_set (store, &iter, QUANTITY, "1", -1);
                row_fixed = TRUE;
            }
            
            // Verify the item date
            if(!isDateValid(date))
            {
                // Invalid item date, replace with date opened
                gtk_list_store_set (store, &iter, DATE,
                                    date_opened, -1);
                row_fixed = TRUE;
            }

        }
        if (row_fixed) ++fixed_for_invoice;
        
        // Move to the next row, skipping all rows of the current invoice if it has a row with an error.
        if (ignore_invoice)
        {
            // Skip all rows of the current invoice.
            // Get the next row and its id.
            iter = first_row_of_invoice;
            while (valid && g_strcmp0 (id, running_id->str) == 0)
            {
                (*deleted)++;
                valid = gtk_list_store_remove (store, &iter);
                if (valid) gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ID, &id, -1);
            }
            // Fixes for ignored invoices don't count in the statistics.
            fixed_for_invoice = 0;
            
            ignore_invoice = FALSE;
        }
        else
        {
            // Get the next row and its id.
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter);
            if (valid) gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ID, &id, -1);
        }
        
        // If the id of the next row is blank, it takes the id of the previous row.
        if (valid && strlen(id) == 0)
        {
            strcpy( id, running_id->str);
            gtk_list_store_set (store, &iter, ID, id, -1);
        }
        
        // If this row was the last row of the invoice...
        if (!valid || (valid && g_strcmp0 (id, running_id->str) != 0))
        {
            on_first_row_of_invoice = TRUE;
            (*fixed) += fixed_for_invoice;
            fixed_for_invoice = 0;
            
            g_free (id);
            g_free (date_opened);
            g_free (date_posted);
            g_free (due_date);
            g_free (account_posted);
            g_free (owner_id);
        }
        else on_first_row_of_invoice = FALSE;

        g_free (date);
        g_free (account);
        g_free (quantity);
        g_free (price);

        row++;
    }
    
    // Deallocate strings.
    g_string_free (running_id, TRUE);

}


/***********************************************************************
 * @todo Maybe invoice checking should be done in gnc_bi_import_fix_bis (...)
 * rather than in here?  But that is more concerned with ensuring the csv is consistent.
 * @param GtkListStore *store
 * @param guint *n_invoices_created
 * @param guint *n_invoices_updated
 * @return void
 ***********************************************************************/
void
gnc_bi_import_create_bis (GtkListStore * store, QofBook * book,
                          guint * n_invoices_created,
                          guint * n_invoices_updated,
                          gchar * type, gchar * open_mode, GString * info,
                          GtkWindow *parent)
{
    gboolean valid, on_first_row_of_invoice, invoice_posted;
    GtkTreeIter iter, first_row_of_invoice;
    gchar *id = NULL, *date_opened = NULL, *owner_id = NULL, *billing_id = NULL, *notes = NULL;
    gchar *date = NULL, *desc = NULL, *action = NULL, *account = NULL, *quantity = NULL,
          *price = NULL, *disc_type = NULL, *disc_how = NULL, *discount = NULL, *taxable = NULL,
          *taxincluded = NULL, *tax_table = NULL;
    gchar *date_posted = NULL, *due_date = NULL, *account_posted = NULL, *memo_posted = NULL,
          *accumulatesplits = NULL;
    guint dummy;
    GncInvoice *invoice;
    GncEntry *entry;
    gint day, month, year;
    gnc_numeric value;
    GncOwner *owner;
    Account *acc = NULL;
    enum update {YES = GTK_RESPONSE_YES, NO = GTK_RESPONSE_NO} update;
    GtkWidget *dialog;
    time64 today;
    InvoiceWindow *iw;
    gint64 denom = 0;
    gnc_commodity *currency;
    GString *running_id;

    // these arguments are needed
    g_return_if_fail (store && book);
    // logic of this function only works for bills or invoices
    g_return_if_fail ((g_ascii_strcasecmp (type, "INVOICE") == 0) ||
            (g_ascii_strcasecmp (type, "BILL") == 0));

    // allow to call this function without statistics
    if (!n_invoices_created)
        n_invoices_created = &dummy;
    if (!n_invoices_updated)
        n_invoices_updated = &dummy;
    *n_invoices_created = 0;
    *n_invoices_updated = 0;

    invoice = NULL;
    update = NO;
    on_first_row_of_invoice = TRUE;
    running_id = g_string_new("");
    
    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);
    while (valid)
    {
        // Walk through the list, reading each row
        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
                            ID, &id,
                            DATE_OPENED, &date_opened,
                            DATE_POSTED, &date_posted,       // if autoposting requested
                            DUE_DATE, &due_date,             // if autoposting requested
                            ACCOUNT_POSTED, &account_posted, // if autoposting requested
                            MEMO_POSTED, &memo_posted,       // if autoposting requested
                            ACCU_SPLITS, &accumulatesplits,  // if autoposting requested
                            OWNER_ID, &owner_id,
                            BILLING_ID, &billing_id,
                            NOTES, &notes,
                            DATE, &date,
                            DESC, &desc,
                            ACTION, &action,
                            ACCOUNT, &account,
                            QUANTITY, &quantity,
                            PRICE, &price,
                            DISC_TYPE, &disc_type,
                            DISC_HOW, &disc_how,
                            DISCOUNT, &discount,
                            TAXABLE, &taxable,
                            TAXINCLUDED, &taxincluded,
                            TAX_TABLE, &tax_table, -1);

        // TODO:  Assign a new invoice number if one is absent.  BUT we don't want to assign a new invoice for every line!!
        // so we'd have to flag this up somehow or add an option in the import GUI.  The former implies that we make
        // an assumption about what the importer (person) wants to do.  It seems reasonable that a CSV file full of items with
        // If an invoice exists then we add to it in this current schema.
        // no predefined invoice number is a new invoice that's in need of a new number.
        // This was  not designed to satisfy the need for repeat invoices however, so maybe we need a another method for this, after all
        // It should be easier to copy an invoice with a new ID than to go through all this malarky.
        
        if (on_first_row_of_invoice)
        {
            g_string_assign(running_id, id);
            first_row_of_invoice = iter;
        }
        
        if (g_ascii_strcasecmp (type, "BILL") == 0)
            invoice = gnc_search_bill_on_id (book, id);
        else if (g_ascii_strcasecmp (type, "INVOICE") == 0)
            invoice = gnc_search_invoice_on_id (book, id);
        DEBUG( "Existing %s ID: %s\n", type, gncInvoiceGetID(invoice));

        // If the search is empty then there is no existing invoice so make a new one
        if (invoice == NULL)
        {
             DEBUG( "Creating a new : %s\n", type );
            // new invoice
            invoice = gncInvoiceCreate (book);
            /* Protect against thrashing the DB and trying to write the invoice
             * record prematurely */
            gncInvoiceBeginEdit (invoice);
            gncInvoiceSetID (invoice, id);
            owner = gncOwnerNew ();
            if (g_ascii_strcasecmp (type, "BILL") == 0)
                gncOwnerInitVendor (owner,
                                    gnc_search_vendor_on_id (book, owner_id));
            else if (g_ascii_strcasecmp (type, "INVOICE") == 0)
                gncOwnerInitCustomer (owner,
                                      gnc_search_customer_on_id (book, owner_id));
            gncInvoiceSetOwner (invoice, owner);
            gncInvoiceSetCurrency (invoice, gncOwnerGetCurrency (owner));	// Set the invoice currency based on the owner
            qof_scan_date (date_opened, &day, &month, &year);
            gncInvoiceSetDateOpened (invoice,
                                     gnc_dmy2time64 (day, month, year));
            gncInvoiceSetBillingID (invoice, billing_id ? billing_id : "");
            notes = un_escape(notes);
            gncInvoiceSetNotes (invoice, notes ? notes : "");
            gncInvoiceSetActive (invoice, TRUE);
            //if (g_ascii_strcasecmp(type,"INVOICE"))gncInvoiceSetBillTo( invoice, billto );
            (*n_invoices_created)++;
            update = YES;

            gncInvoiceCommitEdit (invoice);
        }
// I want to warn the user that an existing billvoice exists, but not every
// time.
// An import can contain many lines usually referring to the same invoice.
// NB: Posted invoices are NEVER updated.
        else			// if invoice exists
        {
            if (gncInvoiceIsPosted (invoice))	// Is it already posted?
            {
                valid =
                    gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter);
                continue;		// If already posted then never import
            }
            if (update != YES)	// Pop up a dialog to ask if updates are the expected action
            {
                dialog = gtk_message_dialog_new (parent,
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_YES_NO,
                                                 "%s",
                                                 _("Are you sure you have bills/invoices to update?"));
                update = gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
                if (update == NO)
                {
                    // Cleanup and leave
                    g_free (id);
                    g_free (date_opened);
                    g_free (owner_id);
                    g_free (billing_id);
                    g_free (notes);
                    g_free (date);
                    g_free (desc);
                    g_free (action);
                    g_free (account);
                    g_free (quantity);
                    g_free (price);
                    g_free (disc_type);
                    g_free (disc_how);
                    g_free (discount);
                    g_free (taxable);
                    g_free (taxincluded);
                    g_free (tax_table);
                    g_free (date_posted);
                    g_free (due_date);
                    g_free (account_posted);
                    g_free (memo_posted);
                    g_free (accumulatesplits);
                    return;
                }
            }
            (*n_invoices_updated)++;
        }


        // Add entry to invoice/bill
        entry = gncEntryCreate (book);
        gncEntryBeginEdit(entry);
        currency = gncInvoiceGetCurrency(invoice);
        if (currency) denom = gnc_commodity_get_fraction(currency);
        qof_scan_date (date, &day, &month, &year);
        {
            GDate *date = g_date_new_dmy(day, month, year);
            gncEntrySetDateGDate (entry, date);
            g_date_free (date);
        }
        today = gnc_time (NULL);
        gncEntrySetDateEntered(entry, today);
        // Remove escaped quotes
        desc = un_escape(desc);
        notes = un_escape(notes);
        gncEntrySetDescription (entry, desc);
        gncEntrySetAction (entry, action);
        value = gnc_numeric_zero(); 
        gnc_exp_parser_parse (quantity, &value, NULL);
        // Need to set the denom appropriately else we get stupid rounding errors.
        value = gnc_numeric_convert (value, denom * 100, GNC_HOW_RND_NEVER);
        //DEBUG("qty = %s",gnc_num_dbg_to_string(value));
        gncEntrySetQuantity (entry, value);
        acc = gnc_account_lookup_for_register (gnc_get_current_root_account (),
                                               account);

        if (g_ascii_strcasecmp (type, "BILL") == 0)
        {
            gncEntrySetBillAccount (entry, acc);
            value = gnc_numeric_zero();
            gnc_exp_parser_parse (price, &value, NULL);
            value = gnc_numeric_convert (value, denom * 100, GNC_HOW_RND_NEVER);
            gncEntrySetBillPrice (entry, value);
            gncEntrySetBillTaxable (entry, text2bool (taxable));
            gncEntrySetBillTaxIncluded (entry, text2bool (taxincluded));
            gncEntrySetBillTaxTable (entry, gncTaxTableLookupByName (book, tax_table));
            gncBillAddEntry (invoice, entry);
        }
        else if (g_ascii_strcasecmp (type, "INVOICE") == 0)
        {
            gncEntrySetNotes (entry, notes);
            gncEntrySetInvAccount (entry, acc);
            value = gnc_numeric_zero();
            gnc_exp_parser_parse (price, &value, NULL);
            value = gnc_numeric_convert (value, denom * 100, GNC_HOW_RND_NEVER);
            //DEBUG("price = %s",gnc_num_dbg_to_string(value));
            gncEntrySetInvPrice (entry, value);
            gncEntrySetInvTaxable (entry, text2bool (taxable));
            gncEntrySetInvTaxIncluded (entry, text2bool (taxincluded));
            gncEntrySetInvTaxTable (entry, gncTaxTableLookupByName (book, tax_table));
            value = gnc_numeric_zero();
            gnc_exp_parser_parse (discount, &value, NULL);
            value = gnc_numeric_convert (value, denom * 100, GNC_HOW_RND_NEVER);
            gncEntrySetInvDiscount (entry, value);
            gncEntrySetInvDiscountType (entry, text2disc_type (disc_type));
            gncEntrySetInvDiscountHow (entry, text2disc_how (disc_how));
            gncInvoiceAddEntry (invoice, entry);
        }
        gncEntryCommitEdit(entry);
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter);
        // handle auto posting of invoices
       
        if (valid)
            gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ID, &id, -1);
        else
            id = NULL;
        
        if (g_strcmp0 (id, running_id->str) == 0) // The next row is for the same invoice.
        {
            on_first_row_of_invoice = FALSE;
        }
        else // The next row is for a new invoice; try to post the invoice.
        {
            // Use posting values from the first row of this invoice.
            gtk_tree_model_get (GTK_TREE_MODEL (store), &first_row_of_invoice,
                                ID, &id,
                                DATE_POSTED, &date_posted,
                                DUE_DATE, &due_date,
                                ACCOUNT_POSTED, &account_posted,
                                MEMO_POSTED, &memo_posted,
                                ACCU_SPLITS, &accumulatesplits, -1);
            invoice_posted = FALSE;

            if (strlen(date_posted) != 0)
            {
                // autopost this invoice
                GHashTable *foreign_currs;
                gboolean auto_pay;
                time64 p_date, d_date;
                guint curr_count;
                gboolean scan_date_r;
                scan_date_r = qof_scan_date (date_posted, &day, &month, &year);
                DEBUG("Invoice %s is marked to be posted because...", id);
                DEBUG("qof_scan_date = %d", scan_date_r);
                if (g_ascii_strcasecmp (type, "INVOICE") == 0)
                    auto_pay = gnc_prefs_get_bool (GNC_PREFS_GROUP_INVOICE, GNC_PREF_AUTO_PAY);
                else
                    auto_pay = gnc_prefs_get_bool (GNC_PREFS_GROUP_BILL, GNC_PREF_AUTO_PAY);
                // Do we have any foreign currencies to deal with?
                foreign_currs = gncInvoiceGetForeignCurrencies (invoice);
                curr_count = g_hash_table_size (foreign_currs);
                DEBUG("curr_count = %d",curr_count);
                // Only auto-post if there's a single currency involved
                if(curr_count == 0)
                {
                    acc = gnc_account_lookup_for_register
                          (gnc_get_current_root_account (), account_posted);
                    // Check if the currencies match
                    if(gncInvoiceGetCurrency(invoice) == gnc_account_get_currency_or_parent(acc))
                    {
                        qof_scan_date (date_posted, &day, &month, &year);
                        p_date = gnc_dmy2time64 (day, month, year);
                        qof_scan_date (due_date, &day, &month, &year);
                        d_date = gnc_dmy2time64 (day, month, year);
                        gncInvoicePostToAccount (invoice, acc, p_date, d_date,
                                             memo_posted,
                                             text2bool (accumulatesplits),
                                             auto_pay);
                        PWARN("Invoice %s posted",id);
                        invoice_posted = TRUE;
                        g_string_append_printf (info, _("Invoice %s posted.\n"),id);
                    }
                    else // No match! Don't post it.
                    {
                        PWARN("Invoice %s NOT posted because currencies don't match", id);
                        g_string_append_printf (info,_("Invoice %s NOT posted because currencies don't match.\n"), id);
                    }
                }
                else
                {
                    PWARN("Invoice %s NOT posted because it requires currency conversion.",id);
                    g_string_append_printf (info,_("Invoice %s NOT posted because it requires currency conversion.\n"),id);
                }
                g_hash_table_unref (foreign_currs);
            }
            else
            {
                PWARN("Invoice %s is NOT marked for posting",id);
            }
        
            // open new bill / invoice in a tab, if requested
            if (g_ascii_strcasecmp(open_mode, "ALL") == 0
                    || (g_ascii_strcasecmp(open_mode, "NOT_POSTED") == 0
                        && !invoice_posted))
            {
                iw =  gnc_ui_invoice_edit (parent, invoice);
                gnc_plugin_page_invoice_new (iw);
            }
            
            // The next row will be for a new invoice.
            on_first_row_of_invoice = TRUE;
        }
    }
    // cleanup
    g_free (id);
    g_free (date_opened);
    g_free (owner_id);
    g_free (billing_id);
    g_free (notes);
    g_free (date);
    g_free (desc);
    g_free (action);
    g_free (account);
    g_free (quantity);
    g_free (price);
    g_free (disc_type);
    g_free (disc_how);
    g_free (discount);
    g_free (taxable);
    g_free (taxincluded);
    g_free (tax_table);
    g_free (date_posted);
    g_free (due_date);
    g_free (account_posted);
    g_free (memo_posted);
    g_free (accumulatesplits);
    
    g_string_free (running_id, TRUE);
}

/* Change any escaped quotes ("") to (")
 * @param char* String to be modified
 * @return char* Modified string.
*/
static char*
un_escape(char *str)
{
    gchar quote = '"';
    gchar *newStr = NULL, *tmpstr = str;
    int n = strlen (str), i;
    newStr = g_malloc (n + 1);
    memset (newStr, 0, n + 1);

    for (i = 0; *tmpstr != '\0'; ++i, ++tmpstr)
    {
        newStr[i] = *tmpstr == quote ? *(++tmpstr) : *(tmpstr);
        if (*tmpstr == '\0')
            break;
    }
    g_free (str);
    return newStr;
}
