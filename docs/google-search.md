# Getting Guts&Bolts on Google

The site now has pages search engines can read (worker/seo.js):

| Address | What it is |
| --- | --- |
| `/games`, `/games/<id>` | the games list, and each public game |
| `/items/<id>` | a catalog or Library item |
| `/videos`, `/videos/<id>` | videos (watchable right on the page) |
| `/art`, `/art/<id>` | art pieces |
| `/paste`, `/paste/<id>` | public pastes (unlisted ones say "noindex") |
| `/academy`, `/academy/<id>` | published courses |
| `/groups`, `/groups/<id>` | groups |
| `/users/<number>` | profiles |
| `/sitemap.xml` | every page above, plus home, products and the legal pages |

Each one has a title, description, canonical address, share card and structured data
(schema.org JSON-LD), and a button that opens the same thing in the web app. The web app's own
`#/...` addresses can't be found on Google by themselves: search engines ignore everything
after `#`, which is why these pages exist.

## Telling Google about the site (do this once)

1. Go to <https://search.google.com/search-console> and sign in with the Google account
   that should own the site.
2. **Add property.** The easiest choice is **Domain** with `gutsandbolts.net`: Google gives you
   a TXT record. Because the domain is on Cloudflare, Search Console usually offers to add it
   for you ("Verify with Cloudflare"). Otherwise add the TXT record in the Cloudflare dashboard:
   DNS > Records > Add record > Type TXT, Name `@`, Content = what Google gave you.
3. **Or** pick **URL prefix** with `https://gutsandbolts.net/` and the **HTML tag** method. Google
   shows something like `<meta name="google-site-verification" content="AbC123...">`. Copy only
   the `content` part and put it in the Cloudflare dashboard: Workers & Pages >
   project-guts-and-bolts > Settings > Variables and Secrets > Add variable
   `GOOGLE_SITE_VERIFICATION` = `AbC123...`. Every page then carries the tag. Press **Verify**.
4. In Search Console, open **Sitemaps** and add `https://gutsandbolts.net/sitemap.xml`.
5. Optional: open **URL inspection**, paste the home page and a game page, and press
   **Request indexing** to speed up the first visit.
6. Bing (and DuckDuckGo, which uses Bing) can copy all of this from Search Console:
   <https://www.bing.com/webmasters> > Import from Google Search Console.

## What to expect

- Google decides what to show and when. New sites usually take a few days to a few weeks
  before pages show up, and ranking well for words like "free games" takes time and links
  from other sites. Nothing here can promise a spot on the first page.
- Searching for the site's own name ("Guts and Bolts game") is the first thing that should work.
- The Pages report in Search Console says which pages are in Google and why others aren't.
- The site is 18+ with cartoon gore, so Google SafeSearch may hide some pages from people who
  have SafeSearch on.
- Pages only show what a signed-out visitor could see. Private and under-review things are
  404s, and checkout and the owner console say "noindex".
