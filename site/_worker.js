const DOWNLOAD = /^\/downloads\/(KeyScribe-(?:macos-(?:arm64|x64)-\d+\.\d+\.\d+\.dmg|windows-x64-\d+\.\d+\.\d+\.(?:exe|msix)))$/;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (!url.pathname.startsWith('/downloads/')) return env.ASSETS.fetch(request);
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return new Response('Method not allowed', { status: 405, headers: { Allow: 'GET, HEAD' } });
    }
    const match = DOWNLOAD.exec(url.pathname);
    if (!match) return new Response('Not found', { status: 404 });
    if (!env.KEYSCRIBE_RELEASES) return new Response('Download storage is not configured', { status: 503 });

    const filename = match[1];
    const version = filename.match(/-(\d+\.\d+\.\d+)\./)[1];
    const key = `releases/${version}/${filename}`;
    const requestedRange = request.headers.has('Range');
    let object;
    try {
      object = request.method === 'HEAD'
        ? await env.KEYSCRIBE_RELEASES.head(key)
        : await env.KEYSCRIBE_RELEASES.get(key, requestedRange ? { range: request.headers } : undefined);
    } catch {
      return new Response('Invalid range', { status: 416 });
    }
    if (!object) return new Response('Not found', { status: 404 });

    const headers = new Headers({
      'Content-Type': filename.endsWith('.dmg') ? 'application/x-apple-diskimage' : 'application/octet-stream',
      'Content-Disposition': `attachment; filename="${filename}"`,
      'Accept-Ranges': 'bytes',
      'ETag': object.httpEtag,
      'Cache-Control': 'public, max-age=31536000, immutable',
    });
    let status = 200;
    if (requestedRange && object.range) {
      const start = object.range.offset ?? Math.max(0, object.size - object.range.suffix);
      const length = object.range.length ?? object.size - start;
      headers.set('Content-Range', `bytes ${start}-${start + length - 1}/${object.size}`);
      headers.set('Content-Length', String(length));
      status = 206;
    } else {
      headers.set('Content-Length', String(object.size));
    }
    return new Response(request.method === 'HEAD' ? null : object.body, { status, headers });
  },
};
