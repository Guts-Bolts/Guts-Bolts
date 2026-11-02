-- Restrict the Worker role to the exact operations used by the activity service.
-- Supabase default table grants can be broader than the explicit grants in the
-- initial migration, so revoke them before granting the least-privilege set.

revoke all on table public.gb_notifications from service_role;
grant select, insert, update on table public.gb_notifications to service_role;

revoke all on table public.gb_auction_events from service_role;
grant select, insert on table public.gb_auction_events to service_role;

revoke all on table public.gb_forum_activity from service_role;
grant select, insert on table public.gb_forum_activity to service_role;
