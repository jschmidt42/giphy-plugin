define(function (require) {
    'use strict';

    const stingray = require('stingray');
    const httpClient = require('common/http-client');

    const GIPHY_PUBLIC_API_KEY = 'dc6zaTOxFJmzC';

    /**
     * Giphy Web API client.
     */
    class GiphyClient {
        constructor () {
            this.giphyAPI = httpClient('http://api.giphy.com/v1/gifs');
        }

        /**
         * Fetch trending Giphies.
         * @returns {Promise.<GiphyResult>}
         */
        fetchTrending () {
            return this.giphyAPI.get('trending', {api_key: GIPHY_PUBLIC_API_KEY})
                .then(result => this.parseResult(result));
        }

        /**
         * Show for new Giphy and restrict to PG-13.
         * @param {string} query - Query keywords
         * @returns {Promise.<GiphyResult>}
         */
        search (query) {
            return this.giphyAPI.get('search', {
                q: query.replace(/[\s]/g, '+'),
                limit: 20,
                rating: 'pg-13',
                api_key: GIPHY_PUBLIC_API_KEY
            }).then(result => this.parseResult(result));
        }

        /**
         * Parse Giphy query results and returned internal Giphy result.
         * @returns {Promise.<GiphyResult>}
         */
        parseResult (result) {
            if (_.size(result.data) <= 0)
                throw new Error('Result has no data');

            return result.data.map(giphy => {
                if (!_.isObject(giphy.images))
                    throw new Error('Result has no images');
                let images = giphy.images;
                return {
                    id: giphy.id,
                    name: (giphy.caption || giphy.slug || giphy.id).replace(/[-]/g, ' '),
                    thumbnail: images.fixed_height_small.url,
                    url: images.original.url,
                    width: images.original.width,
                    height: images.original.height,
                };
            });
        }

        /**
         * Download a Giphy's GIF image.
         */
        download (id, url, dir) {
            let outputGifFilePath = stingray.path.join(dir, `${id}.gif`);
            let downloadClient = httpClient(url);
            return downloadClient.downloadFile(outputGifFilePath).then(() => outputGifFilePath);
        }
    }

    return new GiphyClient();
});